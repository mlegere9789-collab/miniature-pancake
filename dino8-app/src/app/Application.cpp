#include "app/Application.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "imgui.h"
#include "imgui_internal.h"
#include "io/File3dm.h"
#include "io/FileExchange.h"
#include "render/ImageIO.h"
#include "ui/Icons.h"
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
void RegisterAnnotate2Commands(CommandEngine&);
void RegisterCurves2Commands(CommandEngine&);
void RegisterSrfEditCommands(CommandEngine&);
void RegisterCurveEditCommands(CommandEngine&);
void RegisterSurfaceCommands(CommandEngine&);
void RegisterMeshToolsCommands(CommandEngine&);
void RegisterSubDCommands(CommandEngine&);
void RegisterRenderCommands(CommandEngine&);
void RegisterSolidToolsCommands(CommandEngine&);
void UpdateCageCaptives(Document&);  // cmd_solidtools.cpp: re-deforms CageEdit captives when a cage moved
void RegisterSelect2Commands(CommandEngine&);
void RegisterStateCommands(CommandEngine&);
void RegisterViewToolsCommands(CommandEngine&);

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
  if (last_render_.texture) renderer_.DeleteTexture(last_render_.texture);
  last_render_ = RenderImage{};
  renderer_.Shutdown();
}

Viewport::FrameContext Application::MakeFrameContext() {
  Viewport::SetLightTheme(light_theme);
  Viewport::FrameContext ctx;
  ctx.doc = &doc_;
  ctx.preview_lines = &engine_->PreviewLines();
  ctx.preview_points = &engine_->PreviewPoints();
  ctx.show_control_points_for_selected = show_control_points_for_selected;
  ctx.curve_tolerance = curve_display_tolerance;
  ctx.surface_tolerance = surface_display_tolerance;
  ctx.fallback_analysis = &analysis_fallback;
  return ctx;
}

bool Application::RenderView(Viewport* vp, int width, int height, int supersample, bool arctic, std::string& error) {
  if (!vp) vp = ActiveViewport();
  if (!vp) { error = "No active viewport"; return false; }
  if (!renderer_ok_) { error = "Renderer not initialised"; return false; }
  if (width <= 0) width = doc_.Render().render_width;
  if (height <= 0) height = doc_.Render().render_height;
  if (supersample <= 0) supersample = doc_.Render().render_quality;
  Viewport::FrameContext ctx = MakeFrameContext();
  ctx.preview_lines = nullptr;
  ctx.preview_points = nullptr;
  // The Render command's own image has finer tessellation than the viewport.
  ctx.surface_tolerance = std::min(surface_display_tolerance, 0.02);
  ctx.curve_tolerance = std::min(curve_display_tolerance, 0.01);
  const auto t0 = std::chrono::steady_clock::now();
  std::vector<unsigned char> rgb;
  if (!vp->RenderToImage(renderer_, ctx, width, height, supersample, arctic, rgb, error)) return false;
  // The display meshes were rebuilt at the finer tolerance; drop them so
  // the viewports come back at their own setting.
  for (SceneObject& o : doc_.Objects()) o.InvalidateDisplay();
  if (last_render_.texture) renderer_.DeleteTexture(last_render_.texture);
  last_render_ = RenderImage{};
  last_render_.width = width;
  last_render_.height = height;
  last_render_.rgb = std::move(rgb);
  last_render_.view_name = vp->Name();
  last_render_.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  last_render_.texture = renderer_.CreateTexture(width, height, last_render_.rgb.data(), 3);
  panels_.render_window = true;
  return true;
}

bool Application::SaveLastRender(const std::string& path, std::string& error) {
  if (!last_render_.Valid()) { error = "Nothing has been rendered yet (run Render first)"; return false; }
  if (!SaveImageRGB(path, last_render_.width, last_render_.height, last_render_.rgb, error)) return false;
  last_render_.last_saved_path = path;
  return true;
}

void Application::CloseRenderWindow() { panels_.render_window = false; }

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
  RegisterAnnotate2Commands(*engine_);  // dimensions, linetypes, hatch and block extras
  RegisterSelect2Commands(*engine_);
  RegisterStateCommands(*engine_);
  RegisterCurves2Commands(*engine_);
  RegisterSrfEditCommands(*engine_);
  RegisterMeshToolsCommands(*engine_);  // after Transform/Boolean: replaces the simpler Shear/Weld
  RegisterSubDCommands(*engine_);       // SubD editing (creases, ExtrudeSubD, Inset, Bridge...); replaces the Slide stub
  RegisterSolidToolsCommands(*engine_); // holes, curve booleans, cage editing, Flow (replaces the CurveBoolean stub)
  RegisterViewToolsCommands(*engine_);  // clipping planes, layouts, named CPlanes, animation (extends CPlane/ClippingPlane)
  RegisterCurveEditCommands(*engine_);  // replaces the solid-only Intersect/Split registrations
  RegisterSurfaceCommands(*engine_);    // Sweep/Pipe/OffsetSrf/Project... (approximate NURBS/mesh results)
  RegisterRenderCommands(*engine_);     // last: replaces the Render/RenderPreview/Materials placeholders
}

Viewport* Application::ActiveViewport() {
  if (active_layout_ >= 0) {
    if (Viewport* d = DetailViewport(active_detail_)) return d;
    if (page_viewport_) return page_viewport_.get();
  }
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
  if (page_viewport_ && ToLower(page_viewport_->Name()) == ToLower(name)) return page_viewport_.get();
  for (auto& vp : detail_viewports_) {
    if (ToLower(vp->Name()) == ToLower(name)) return vp.get();
  }
  return nullptr;
}

Viewport* Application::AddViewport(const std::string& name, const std::string& standard_view, bool floating) {
  std::string unique = name;
  for (int i = 2; FindViewport(unique); ++i) unique = name + " " + std::to_string(i);
  viewports_.push_back(std::make_unique<Viewport>(unique, standard_view));
  viewports_.back()->SetFloating(floating);
  for (auto& v : viewports_) v->SetActive(false);
  viewports_.back()->SetActive(true);
  active_viewport_ = static_cast<int>(viewports_.size()) - 1;
  layout_built_ = false;
  return viewports_.back().get();
}

bool Application::RemoveViewport(const std::string& name) {
  if (viewports_.size() <= 1) return false;
  for (size_t i = 0; i < viewports_.size(); ++i) {
    if (ToLower(viewports_[i]->Name()) != ToLower(name)) continue;
    const bool was_active = viewports_[i]->IsActive();
    viewports_.erase(viewports_.begin() + static_cast<long>(i));
    active_viewport_ = std::clamp(active_viewport_, 0, static_cast<int>(viewports_.size()) - 1);
    if (was_active) viewports_[static_cast<size_t>(std::min<int>(static_cast<int>(i), static_cast<int>(viewports_.size()) - 1))]->SetActive(true);
    layout_built_ = false;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Layouts
// ---------------------------------------------------------------------------

Layout* Application::ActiveLayout() {
  if (active_layout_ < 0 || active_layout_ >= static_cast<int>(doc_.Layouts().size())) return nullptr;
  return &doc_.Layouts()[static_cast<size_t>(active_layout_)];
}

Viewport* Application::DetailViewport(int index) {
  if (index < 0 || index >= static_cast<int>(detail_viewports_.size())) return nullptr;
  return detail_viewports_[static_cast<size_t>(index)].get();
}

bool Application::SetActiveLayout(int index) {
  if (index >= static_cast<int>(doc_.Layouts().size())) return false;
  if (index < 0) index = -1;
  if (index == active_layout_ && (index < 0 || page_viewport_)) return true;
  active_layout_ = index;
  active_detail_ = -1;
  page_viewport_.reset();
  detail_viewports_.clear();
  if (Layout* L = ActiveLayout()) {
    page_viewport_ = std::make_unique<Viewport>(L->name, "Top");
    page_viewport_->SetPage(L->width_mm, L->height_mm);
    page_viewport_->SetMode(DisplayMode::Shaded);
    page_viewport_->ZoomTo(kernel::BoundingBox{kernel::Point3d(-L->width_mm * 0.05, -L->height_mm * 0.05, -1),
                                               kernel::Point3d(L->width_mm * 1.05, L->height_mm * 1.05, 1)});
    // Fit the sheet to a typical (wide) page window rather than its diagonal.
    page_viewport_->GetCamera().State().ortho_height = std::max(L->height_mm, L->width_mm / 2.0) * 1.15;
    page_viewport_->SetActive(true);
    SyncDetailViewports();
  }
  layout_built_ = false;  // the dock layout swaps between the viewport grid and the page
  return true;
}

bool Application::SetActiveLayoutByName(const std::string& name) {
  if (ToLower(name) == "model") return SetActiveLayout(-1);
  for (size_t i = 0; i < doc_.Layouts().size(); ++i) {
    if (ToLower(doc_.Layouts()[i].name) == ToLower(name)) return SetActiveLayout(static_cast<int>(i));
  }
  return false;
}

void Application::SyncDetailViewports() {
  Layout* L = ActiveLayout();
  if (!L) { detail_viewports_.clear(); active_detail_ = -1; return; }
  std::vector<std::unique_ptr<Viewport>> old = std::move(detail_viewports_);
  detail_viewports_.clear();
  for (LayoutDetail& d : L->details) {
    std::unique_ptr<Viewport> vp;
    for (auto& o : old) {
      if (o && o->Name() == d.name) { vp = std::move(o); break; }
    }
    if (!vp) {
      vp = std::make_unique<Viewport>(d.name, d.standard_view);
      vp->GetCamera().SetState(d.camera);
      vp->SetMode(DisplayModeFromName(d.display_mode));
    }
    detail_viewports_.push_back(std::move(vp));
  }
  if (active_detail_ >= static_cast<int>(detail_viewports_.size())) active_detail_ = -1;
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
  if (!panels_.notifications) ++unread_notifications;
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
    DrawPopupToolbarGrid();
    ImGui::EndPopup();
  }
}

// The middle-mouse popup: the most recently used commands first (Rhino's
// "recent commands" popup), then a fixed grid of everyday tools.
void Application::DrawPopupToolbarGrid() {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
  const bool saved_labels = toolbar_labels;
  toolbar_labels = true;
  std::vector<std::string> recent;
  for (const std::string& r : engine_->RecentCommands()) {
    if (std::find(recent.begin(), recent.end(), r) == recent.end()) recent.push_back(r);
    if (recent.size() >= 6) break;
  }
  auto row = [&](const std::vector<std::string>& cmds) {
    for (size_t i = 0; i < cmds.size(); ++i) {
      if (i) ImGui::SameLine();
      const char* label = ToolbarButtonLabel(cmds[i]);
      IconButtonResult r = IconButton(*this, cmds[i].c_str(), label ? label : cmds[i].c_str(), true, false);
      if (r.left) { engine_->Execute(cmds[i]); ImGui::CloseCurrentPopup(); }
    }
  };
  if (!recent.empty()) {
    ImGui::TextDisabled("Recent");
    row(recent);
    ImGui::Separator();
  }
  row({"Move", "Copy", "Rotate", "Scale", "Mirror", "Delete"});
  row({"Line", "Polyline", "Circle", "Rectangle", "Box", "Sphere"});
  row({"Join", "Explode", "Trim", "Split", "Extend", "Offset"});
  row({"Undo", "Redo", "Hide", "Show", "ZoomExtents", "SelAll"});
  toolbar_labels = saved_labels;
  ImGui::PopStyleVar();
}

// Right-click menu in a viewport: on an object, or on empty space.
void Application::DrawContextMenu() {
  if (open_context_menu_) {
    ImGui::OpenPopup("##vp_context");
    ImGui::SetNextWindowPos(context_menu_pos_, ImGuiCond_Always);
    open_context_menu_ = false;
  }
  if (!ImGui::BeginPopup("##vp_context")) return;
  auto item = [&](const char* label, const char* command, const char* shortcut = nullptr) {
    ImGui::PushID(command);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float ih = ImGui::GetTextLineHeight();
    ImGui::Dummy(ImVec2(ih + 4, ih));
    ImGui::SameLine(0, 0);
    DrawIcon(ImGui::GetWindowDrawList(), command, ImVec2(p.x, p.y), ih, ImGui::GetColorU32(ImGuiCol_Text), ThemeColors::AccentU32());
    const bool hit = ImGui::MenuItem(label, shortcut);
    ImGui::PopID();
    if (hit) engine_->Execute(command);
  };
  SceneObject* obj = context_menu_object_ != kNoObject ? doc_.Find(context_menu_object_) : nullptr;
  if (obj) {
    const std::string name = obj->name.empty() ? std::string("Object ") + std::to_string(static_cast<long long>(context_menu_object_)) : obj->name;
    ImGui::TextColored(ThemeColors::Accent(), "%s", name.c_str());
    ImGui::Separator();
    item("Hide", "Hide", "Ctrl+H");
    item("Lock", "Lock");
    item("Delete", "Delete", "Del");
    ImGui::Separator();
    item("Copy", "CopyToClipboard", "Ctrl+C");
    item("Paste", "Paste", "Ctrl+V");
    ImGui::Separator();
    if (obj->group_id >= 0) item("Ungroup", "Ungroup"); else item("Group", "Group", "Ctrl+G");
    item("Isolate", "Isolate");
    item("Properties", "Properties", "F3");
    item("Zoom Selected", "ZoomSelected");
    if (ImGui::BeginMenu("Object display mode")) {
      Viewport* vp = ActiveViewport();
      for (DisplayMode m : AllDisplayModes()) {
        if (ImGui::MenuItem(DisplayModeName(m), nullptr, vp && vp->Mode() == m) && vp) vp->SetMode(m);
      }
      ImGui::EndMenu();
    }
  } else {
    if (!engine_->LastCommand().empty()) {
      const std::string label = "Repeat " + engine_->LastCommand();
      if (ImGui::MenuItem(label.c_str(), "Enter")) engine_->RepeatLast();
      ImGui::Separator();
    }
    item("Undo", "Undo", "Ctrl+Z");
    item("Redo", "Redo", "Ctrl+Y");
    item("Paste", "Paste", "Ctrl+V");
    item("Select All", "SelAll", "Ctrl+A");
    item("Zoom Extents", "ZoomExtents");
    ImGui::Separator();
    DrawPopupToolbarGrid();
  }
  ImGui::EndPopup();
}

// First-run welcome card on an empty document.
void Application::DrawWelcomeOverlay() {
  // Never in headless smoke runs (it would sit over scripted viewport picks)
  // unless a screenshot run asks for it explicitly.
  if (smoke_mode && !std::getenv("DINO8_SHOW_WELCOME")) return;
  if (welcome_dismissed || welcome_closed_this_session_) return;
  if (doc_.ObjectCount() > 0 || !doc_.Path().empty() || engine_->IsRunning()) return;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float left = LeftSidebarWidth(*this);
  const float top = ToolbarHeight(*this) + CommandLineHeight();
  const ImVec2 centre(vp->WorkPos.x + left + (vp->WorkSize.x - left) * 0.40f, vp->WorkPos.y + top + (vp->WorkSize.y - top - StatusBarHeight()) * 0.45f);
  ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0));
  ImGui::SetNextWindowBgAlpha(0.97f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  if (ImGui::Begin("##welcome", nullptr, flags)) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    DrawIcon(dl, "Box", p, 40.0f, ImGui::GetColorU32(ImGuiCol_Text), ThemeColors::AccentU32());
    ImGui::Dummy(ImVec2(46, 40));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, 22.0f * ImGui::GetStyle().FontScaleMain);
    ImGui::TextColored(ThemeColors::Accent(), "Welcome to Dino 8");
    ImGui::PopFont();
    ImGui::TextDisabled("Free NURBS / SubD / mesh modeler, version " DINO8_VERSION);
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetWindowWidth() - 44);
    if (ImGui::SmallButton("x")) welcome_closed_this_session_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close (shows again on the next empty document)");
    ImGui::Spacing();
    const ImVec2 bsz(254, 0);
    if (ImGui::Button("New model", bsz)) welcome_closed_this_session_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Open...", bsz)) { welcome_closed_this_session_ = true; engine_->Execute("Open"); }
    if (ImGui::Button("Learn  (F1)", bsz)) { welcome_closed_this_session_ = true; panels_.help = true; }
    ImGui::SameLine();
    if (ImGui::Button("Command list  (Ctrl+F1)", bsz)) { welcome_closed_this_session_ = true; panels_.command_list = true; }
    if (!recent_files_.empty()) {
      ImGui::Spacing();
      ImGui::TextDisabled("Recent files");
      int shown = 0;
      for (const std::string& f : recent_files_) {
        if (shown++ >= 5) break;
        const std::string name = fs::path(f).filename().string();
        ImGui::PushID(f.c_str());
        if (ImGui::Selectable(name.c_str())) {
          std::string e;
          welcome_closed_this_session_ = true;
          if (!OpenDocument(f, e)) Notify(e);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.c_str());
        ImGui::PopID();
      }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("Dino 8 is free software. No accounts, licenses or subscriptions, ever.");
    ImGui::TextDisabled("Start typing a command, or pick a tool from the toolbars.");
    bool dont = welcome_dismissed;
    if (ImGui::Checkbox("Don't show this again", &dont)) welcome_dismissed = dont;
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
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
  SetActiveLayout(-1);
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
  } else if (ext == ".ply") {
    ok = ImportPly(fresh, path, error);
  } else if (ext == ".dxf") {
    ok = ImportDxf(fresh, path, error);
  } else {
    error = "Unsupported file type: " + ext;
  }
  if (!ok) return false;
  SetActiveLayout(-1);
  doc_ = std::move(fresh);
  doc_.SetPath(path);
  doc_.SetModified(false);
  AddRecentFile(path);
  ZoomExtentsAll();
  Notify("Opened " + path + " (" + std::to_string(doc_.ObjectCount()) + " objects)");
  if (!error.empty()) Notify(error);  // reader summary / skipped-entity note
  error.clear();
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
  } else if (ext == ".ply") {
    ok = ExportPly(doc_, path, false, error);
    if (ok) Notify("Exported " + path);
  } else if (ext == ".dxf") {
    ok = ExportDxf(doc_, path, false, error);
    if (ok) Notify("Exported " + path);
  } else if (ext == ".svg" || ext == ".pdf") {
    ok = ExportDrawing(path, false, 0.0, error);
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
  } else if (ext == ".ply") {
    ok = ImportPly(doc_, path, error);
  } else if (ext == ".dxf") {
    ok = ImportDxf(doc_, path, error);
  } else {
    error = "Unsupported file type: " + ext;
  }
  if (ok) {
    Notify("Imported " + path);
    if (!error.empty()) Notify(error);
    error.clear();
  }
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
  if (ext == ".dxf") return ExportDxf(doc_, path, true, error);
  if (ext == ".ply") return ExportPly(doc_, path, true, error);
  if (ext == ".svg" || ext == ".pdf") return ExportDrawing(path, true, 0.0, error);
  return ExportMeshFile(doc_, path, true, error);
}

bool Application::ExportDrawing(const std::string& path, bool selected_only, double scale, std::string& error) {
  const std::string ext = ToLower(fs::path(path).extension().string());
  DrawingOptions opts;
  opts.scale = scale;
  // Print through the active viewport; fall back to a Top view when none is
  // active (headless / no viewports).
  const Viewport* view = ActiveViewport();
  if (!view) view = FindViewport("Top");
  bool ok = false;
  if (ext == ".svg") ok = ExportSvg(doc_, view, path, selected_only, opts, error);
  else if (ext == ".pdf") ok = ExportPdf(doc_, view, path, selected_only, opts, error);
  else error = "Unsupported drawing format: " + ext;
  if (ok) Notify((ext == ".pdf" ? "Printed " : "Exported ") + path + (view ? " (" + view->Name() + " view" + (scale > 0 ? ", scale " + std::to_string(scale).substr(0, 4) : "") + ")" : ""));
  return ok;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Application::Frame() {
  if (std::getenv("DINO8_UI_DEBUG") && (ImGui::GetFrameCount() == 5 || ImGui::GetFrameCount() == 90)) { const ImVec4& w = ImGui::GetStyle().Colors[ImGuiCol_WindowBg]; std::fprintf(stderr, "[theme] light=%d WindowBg=%.2f %.2f %.2f a=%.2f\n", light_theme ? 1 : 0, w.x, w.y, w.z, w.w); }
  UpdateCageCaptives(doc_);
  HandleShortcuts();
  ViewToolsFrame(*this);
  DrawDockspace();
  DrawViewports();
  DrawPanels();
  DrawCommandLine();
  DrawStatusBar();
  DrawViewportTabs();
  DrawFileDialog();
  DrawConfirmDiscard();
  DrawPopupToolbar();
  DrawContextMenu();
  DrawWelcomeOverlay();
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
  ImGui::DockBuilderDockWindow("Layouts", right_id);
  ImGui::DockBuilderDockWindow("Clipping Planes", right_id);
  ImGui::DockBuilderDockWindow("Named CPlanes", right_id);
  ImGui::DockBuilderDockWindow("Materials", right_id);
  ImGui::DockBuilderDockWindow("Lights", right_id);
  ImGui::DockBuilderDockWindow("Rendering", right_id);
  ImGui::DockBuilderDockWindow("Environments", right_id);
  ImGui::DockBuilderDockWindow("Textures", right_id);
  ImGui::DockBuilderDockWindow("Display", right_id);
  ImGui::DockBuilderDockWindow("Properties", right_bottom);
  ImGui::DockBuilderDockWindow("Help", right_bottom);
  ImGui::DockBuilderDockWindow("Notes", right_bottom);
  ImGui::DockBuilderDockWindow("Document User Text", right_bottom);
  ImGui::DockBuilderDockWindow("Command History", bottom_id);
  ImGui::DockBuilderDockWindow("Command List", bottom_id);
  ImGui::DockBuilderDockWindow("Notifications", bottom_id);

  // Viewport grid (or the layout page while a layout is active).
  std::vector<Viewport*> docked;
  for (auto& vp : viewports_) if (!vp->Floating()) docked.push_back(vp.get());
  if (active_layout_ >= 0 && page_viewport_) {
    const std::string title = "Layout: " + page_viewport_->Name() + "###vp_page";
    ImGui::DockBuilderDockWindow(title.c_str(), main_id);
  } else if (docked.size() != viewports_.size() || (viewports_.size() != 4 && viewports_.size() != 3)) {
    // Generic grid for any number of docked viewports: ceil(sqrt(n)) columns.
    const int n = static_cast<int>(docked.size());
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n)))));
    const int rows = std::max(1, (n + cols - 1) / cols);
    std::vector<ImGuiID> col_ids;
    ImGuiID rest = main_id;
    for (int c = 0; c < cols; ++c) {
      if (c == cols - 1) { col_ids.push_back(rest); break; }
      ImGuiID left = 0;
      ImGuiID right = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 1.0f - 1.0f / static_cast<float>(cols - c), nullptr, &left);
      col_ids.push_back(left);
      rest = right;
    }
    int k = 0;
    for (int c = 0; c < cols && k < n; ++c) {
      const int in_col = std::min(rows, n - c * rows);
      ImGuiID node = col_ids[static_cast<size_t>(c)];
      for (int r = 0; r < in_col && k < n; ++r, ++k) {
        ImGuiID slot = node;
        if (r < in_col - 1) {
          ImGuiID top = 0;
          ImGuiID bottom = ImGui::DockBuilderSplitNode(node, ImGuiDir_Down, 1.0f - 1.0f / static_cast<float>(in_col - r), nullptr, &top);
          slot = top;
          node = bottom;
        }
        const std::string title = docked[static_cast<size_t>(k)]->Name() + "###vp_" + docked[static_cast<size_t>(k)]->Name();
        ImGui::DockBuilderDockWindow(title.c_str(), slot);
      }
    }
  } else if (viewports_.size() == 4) {
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
  // Reserve space for the toolbar + command line (top), the sidebar (left)
  // and the status bar (bottom).
  const float command_h = CommandLineHeight();
  const float status_h = StatusBarHeight() + ViewportTabsHeight();
  const float toolbar_h = ToolbarHeight(*this);
  const float sidebar_w = LeftSidebarWidth(*this);
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + sidebar_w, vp->WorkPos.y + command_h + toolbar_h));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - sidebar_w, vp->WorkSize.y - command_h - status_h - toolbar_h));
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
  if (active_layout_ >= 0) {
    if (ActiveLayout()) { DrawLayoutPage(); return; }
    SetActiveLayout(-1);  // the layout was deleted (Undo / New)
  }

  // Render every visible viewport into its texture first.
  Viewport::FrameContext ctx = MakeFrameContext();
  ctx.print_display = viewtools.print_display;
  if (want_point && pending_hover_) ctx.cursor_marker = pending_hover_;

  bool request_focus = false;
  std::optional<kernel::Point3d> hover;
  for (size_t i = 0; i < viewports_.size(); ++i) {
    Viewport& vp = *viewports_[i];
    const bool show = !maximized_any || vp.Maximized();
    vp.SetVisible(show);
    if (!show) continue;
    if (!bring_to_top_.empty() && ToLower(bring_to_top_) == ToLower(vp.Name())) {
      ImGui::SetWindowFocus((vp.Name() + "###vp_" + vp.Name()).c_str());
      bring_to_top_.clear();
    }
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

void Application::DrawLayoutPage() {
  Layout* L = ActiveLayout();
  if (!L || !page_viewport_) return;
  const Want want = engine_->CurrentWant();
  const bool want_point = want == Want::Point;
  const bool want_objects = want == Want::Objects;
  std::optional<kernel::Point3d> ortho_base = engine_->LastPoint();
  page_viewport_->SetName(L->name);
  page_viewport_->SetPage(L->width_mm, L->height_mm);
  SyncDetailViewports();

  Viewport::FrameContext ctx;
  ctx.doc = &doc_;
  ctx.preview_lines = &engine_->PreviewLines();
  ctx.preview_points = &engine_->PreviewPoints();
  ctx.show_control_points_for_selected = show_control_points_for_selected;
  ctx.curve_tolerance = curve_display_tolerance;
  ctx.surface_tolerance = surface_display_tolerance;
  ctx.fallback_analysis = &analysis_fallback;
  ctx.print_display = viewtools.print_display;
  ctx.show_clipping_planes = false;
  if (want_point && pending_hover_) ctx.cursor_marker = pending_hover_;

  // Details render with their own cameras and per-detail hiding.
  for (size_t i = 0; i < L->details.size() && i < detail_viewports_.size(); ++i) {
    LayoutDetail& d = L->details[i];
    Viewport& dv = *detail_viewports_[i];
    CameraState& cam = dv.GetCamera().State();
    if (d.scale > 0 && !cam.perspective) cam.ortho_height = d.height / d.scale;
    Viewport::FrameContext dctx = ctx;
    dctx.hidden_layers = &d.hidden_layers;
    dctx.hidden_objects = &d.hidden_objects;
    dctx.show_clipping_planes = true;
    dctx.cursor_marker.reset();
    if (static_cast<int>(i) == active_detail_ && want_point && pending_hover_) dctx.cursor_marker = pending_hover_;
    dv.Render(renderer_, dctx);
  }
  Viewport::FrameContext pctx = ctx;
  pctx.preview_lines = active_detail_ < 0 ? ctx.preview_lines : nullptr;
  pctx.preview_points = active_detail_ < 0 ? ctx.preview_points : nullptr;
  if (active_detail_ >= 0) pctx.cursor_marker.reset();
  page_viewport_->Render(renderer_, pctx);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  const std::string title = "Layout: " + L->name + "###vp_page";
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
  bool request_focus = false;
  std::optional<kernel::Point3d> hover;
  if (ImGui::Begin(title.c_str(), nullptr, flags)) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const int w = std::max(1, static_cast<int>(avail.x)), h = std::max(1, static_cast<int>(avail.y));
    // Detail rectangles in screen pixels (from the page camera).
    struct Rect { float x0, y0, x1, y1; };
    std::vector<Rect> rects;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hovered_detail = -1;
    for (size_t i = 0; i < L->details.size(); ++i) {
      const LayoutDetail& d = L->details[i];
      double ax, ay, bx, by;
      Rect r{0, 0, 0, 0};
      if (page_viewport_->WorldToPixel(kernel::Point3d(d.x, d.y, 0), ax, ay) &&
          page_viewport_->WorldToPixel(kernel::Point3d(d.x + d.width, d.y + d.height, 0), bx, by)) {
        r = Rect{static_cast<float>(origin.x + std::min(ax, bx)), static_cast<float>(origin.y + std::min(ay, by)),
                 static_cast<float>(origin.x + std::max(ax, bx)), static_cast<float>(origin.y + std::max(ay, by))};
      }
      rects.push_back(r);
      if (mouse.x >= r.x0 && mouse.x <= r.x1 && mouse.y >= r.y0 && mouse.y <= r.y1) hovered_detail = static_cast<int>(i);
    }
    // The page: pan/zoom with the usual mouse buttons, point picks in page mm.
    page_viewport_->SetAllInputLocked(active_detail_ >= 0 && hovered_detail == active_detail_);
    page_viewport_->SetVisible(true);
    ViewportEvents pev = page_viewport_->DrawEmbedded(doc_, snaps_, want_point, want_objects, ortho_base, 1.0, request_focus, w, h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < L->details.size() && i < detail_viewports_.size(); ++i) {
      LayoutDetail& d = L->details[i];
      Viewport& dv = *detail_viewports_[i];
      const Rect& r = rects[i];
      const int rw = std::max(1, static_cast<int>(r.x1 - r.x0)), rh = std::max(1, static_cast<int>(r.y1 - r.y0));
      const bool active = static_cast<int>(i) == active_detail_;
      dl->PushClipRect(ImVec2(std::max(r.x0, origin.x), std::max(r.y0, origin.y)), ImVec2(std::min(r.x1, origin.x + w), std::min(r.y1, origin.y + h)), true);
      if (active) {
        ImGui::SetCursorScreenPos(ImVec2(r.x0, r.y0));
        dv.SetVisible(true);
        dv.SetAllInputLocked(d.locked);
        ViewportEvents dev = dv.DrawEmbedded(doc_, snaps_, want_point, want_objects, ortho_base, doc_.Settings().grid_spacing, request_focus, rw, rh);
        if (dev.hovered && dev.hover_pick) hover = dev.hover_pick->point;
        ProcessViewportEvents(dv, dev);
      } else {
        dv.SetAllInputLocked(true);
        dv.SetVisible(true);
        ImGui::SetCursorScreenPos(ImVec2(r.x0, r.y0));
        dv.DrawEmbedded(doc_, snaps_, false, false, std::nullopt, doc_.Settings().grid_spacing, request_focus, rw, rh);
      }
      dl->PopClipRect();
      const ImU32 border = d.selected ? IM_COL32(255, 210, 0, 255) : (active ? IM_COL32(70, 130, 220, 255) : IM_COL32(60, 60, 60, 255));
      dl->AddRect(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), border, 0.0f, 0, d.selected || active ? 2.5f : 1.0f);
      d.camera = dv.GetCamera().State();
      d.display_mode = DisplayModeName(dv.Mode());
    }
    ImGui::SetCursorScreenPos(origin);
    if (pev.hovered && pev.hover_pick && hovered_detail != active_detail_) hover = pev.hover_pick->point;
    if (pev.clicked) {
      if (want_point && pev.click_pick && hovered_detail != active_detail_) {
        engine_->FeedPoint(pev.click_pick->point);
      } else if (hovered_detail >= 0 && hovered_detail != active_detail_) {
        if (pev.double_clicked) {
          active_detail_ = hovered_detail;
        } else {
          for (size_t i = 0; i < L->details.size(); ++i) {
            if (pev.shift || pev.ctrl) { if (static_cast<int>(i) == hovered_detail) L->details[i].selected = pev.ctrl ? !L->details[i].selected : true; }
            else L->details[i].selected = static_cast<int>(i) == hovered_detail;
          }
          active_detail_ = -1;
        }
      } else if (hovered_detail < 0) {
        if (active_detail_ >= 0) active_detail_ = -1;
        else if (!pev.shift && !pev.ctrl) for (LayoutDetail& d : L->details) d.selected = false;
        if (want != Want::Objects && !pev.shift && !pev.ctrl) doc_.SelectNone();
      }
    } else if (pev.double_clicked && hovered_detail < 0) {
      active_detail_ = -1;
    }
    if (pev.right_clicked && hovered_detail != active_detail_) engine_->FeedEnter();
    // Page title / hint.
    char label[160];
    std::snprintf(label, sizeof(label), "%s  %.0f x %.0f mm  %s", L->name.c_str(), L->width_mm, L->height_mm,
                  active_detail_ >= 0 ? "(detail active: double-click the page to return)" : "(double-click a detail to activate it)");
    dl->AddText(ImVec2(origin.x + 8, origin.y + h - ImGui::GetTextLineHeight() - 6), IM_COL32(230, 230, 230, 220), label);
  }
  ImGui::End();
  ImGui::PopStyleVar();
  pending_hover_ = hover;
  engine_->FeedHover(hover);
  if (request_focus) focus_command_line_ = true;
}

float Application::ViewportTabsHeight() const {
  return show_viewport_tabs ? ImGui::GetFrameHeight() + 6.0f : 0.0f;
}

void Application::DrawViewportTabs() {
  if (!show_viewport_tabs) return;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float h = ViewportTabsHeight();
  const float status_h = StatusBarHeight();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - status_h - h));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 3));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
  ImGui::Begin("##ViewportTabs", nullptr, flags);
  auto tab = [&](const char* name, bool current, int index) {
    ImGui::PushStyleColor(ImGuiCol_Button, current ? ImVec4(0.22f, 0.45f, 0.75f, 1) : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
    if (ImGui::SmallButton(name)) SetActiveLayout(index);
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 4);
  };
  tab("Model", active_layout_ < 0, -1);
  for (size_t i = 0; i < doc_.Layouts().size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    tab(doc_.Layouts()[i].name.c_str(), static_cast<int>(i) == active_layout_, static_cast<int>(i));
    ImGui::PopID();
  }
  if (ImGui::SmallButton("+")) engine_->Execute("Layout");
  ImGui::SameLine(0, 12);
  ImGui::TextDisabled(active_layout_ < 0 ? "Model space" : "Layout (paper space, mm)");
  ImGui::End();
  ImGui::PopStyleVar(2);
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
    if (engine_->IsRunning()) {
      // Right click = Enter while a command runs (Rhino convention).
      engine_->FeedEnter();
    } else {
      // Otherwise a context menu: for the object under the cursor (which
      // becomes selected), or for empty space.
      context_menu_object_ = ev.right_click_object;
      if (SceneObject* o = context_menu_object_ != kNoObject ? doc_.Find(context_menu_object_) : nullptr) {
        if (!o->selected) { doc_.SelectNone(); o->selected = true; }
      }
      context_menu_pos_ = ImGui::GetMousePos();
      open_context_menu_ = true;
    }
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

float Application::CommandLineHeight() const {
  return ImGui::GetFrameHeightWithSpacing() * 2.0f + 8.0f;
}

void Application::DrawCommandLine() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float toolbar_h = ToolbarHeight(*this);
  const float h = CommandLineHeight();
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
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  const size_t n = hist.size();
  const std::string line1 = n >= 1 ? hist[n - 1] : "";
  ImGui::TextUnformatted(line1.c_str());
  ImGui::PopStyleColor();

  // Prompt + options + input.
  const std::string prompt = engine_->Prompt();
  ImGui::AlignTextToFramePadding();
  ImGui::PushStyleColor(ImGuiCol_Text, engine_->IsRunning() ? ThemeColors::Accent() : ImGui::GetStyleColorVec4(ImGuiCol_Text));
  ImGui::TextUnformatted(prompt.c_str());
  ImGui::PopStyleColor();
  if (const std::vector<OptionSpec>* opts = engine_->CurrentOptions()) {
    // Option chips: name in muted text, current value in the accent colour.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 2));
    for (const OptionSpec& o : *opts) {
      ImGui::SameLine(0, 6);
      std::string label = o.name;
      if (!o.value.empty()) label += "=" + o.value;
      ImGui::PushStyleColor(ImGuiCol_Button, ThemeColors::Accent(0.18f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ThemeColors::Accent(0.40f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ThemeColors::Accent(0.60f));
      ImGui::PushStyleColor(ImGuiCol_Border, ThemeColors::Accent(0.6f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::Button((label + "##opt_" + o.name).c_str())) engine_->FeedOption(o.name);
      if (!o.value.empty()) {
        // Recolour the value part of the chip.
        const ImVec2 name_sz = ImGui::CalcTextSize((o.name + "=").c_str());
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 8 + name_sz.x, p.y + 2), ThemeColors::AccentU32(), o.value.c_str());
      }
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(4);
      if (ImGui::IsItemHovered()) {
        if (o.toggle) ImGui::SetTooltip("%s: click to toggle (or type %s)", o.name.c_str(), o.name.c_str());
        else if (!o.choices.empty()) ImGui::SetTooltip("%s: click to cycle (or type %s)", o.name.c_str(), o.name.c_str());
        else if (o.numeric) ImGui::SetTooltip("%s: click, then type a new value", o.name.c_str());
        else ImGui::SetTooltip("Option: %s (or type its name)", o.name.c_str());
      }
    }
    ImGui::PopStyleVar(2);
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
        if (self->autocomplete_count_ > 0) {
          // Autocomplete popup open: Up/Down move the highlight instead.
          const int n = self->autocomplete_count_;
          if (data->EventKey == ImGuiKey_DownArrow) self->autocomplete_index_ = (self->autocomplete_index_ + 1) % n;
          else if (data->EventKey == ImGuiKey_UpArrow) self->autocomplete_index_ = self->autocomplete_index_ <= 0 ? n - 1 : self->autocomplete_index_ - 1;
          return 0;
        }
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
    // Enter with an autocomplete row highlighted runs that command.
    if (autocomplete_index_ >= 0 && autocomplete_count_ > 0 && !engine_->IsRunning()) {
      std::vector<const CommandInfo*> m = catalog_.WithPrefix(autocomplete_prefix_, 12);
      if (autocomplete_index_ < static_cast<int>(m.size())) command_input_ = m[static_cast<size_t>(autocomplete_index_)]->name;
    }
    autocomplete_index_ = -1;
    autocomplete_count_ = 0;
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
  // Autocomplete popup while typing a command name: status badge, first
  // help line, Up/Down to move, Tab to complete, Enter to run.
  bool showing = false;
  if (input_active && !engine_->IsRunning() && !command_input_.empty() && command_input_.find(' ') == std::string::npos) {
    std::string prefix = command_input_;
    while (!prefix.empty() && (prefix.front() == '_' || prefix.front() == '-' || prefix.front() == '!')) prefix.erase(prefix.begin());
    std::vector<const CommandInfo*> matches = catalog_.WithPrefix(prefix, 12);
    if (!matches.empty() && !prefix.empty()) {
      showing = true;
      if (prefix != autocomplete_prefix_) autocomplete_index_ = -1;
      autocomplete_prefix_ = prefix;
      autocomplete_count_ = static_cast<int>(matches.size());
      if (autocomplete_index_ >= autocomplete_count_) autocomplete_index_ = -1;
      const ImVec2 pos = ImGui::GetItemRectMin();
      const ImVec2 size = ImGui::GetItemRectSize();
      ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + size.y + 2));
      ImGui::SetNextWindowSize(ImVec2(std::min(size.x, 760.0f), 0));
      const ImGuiWindowFlags pflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking;
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 2));
      ImGui::Begin("##autocomplete", nullptr, pflags);
      if (ImGui::BeginTable("ac", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, 108.0f);
        ImGui::TableSetupColumn("desc", ImGuiTableColumnFlags_WidthStretch);
        for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
          const CommandInfo* c = matches[static_cast<size_t>(i)];
          const RegisteredCommand* r = engine_->Find(c->name);
          const CommandStatus st = r ? r->status : CommandStatus::Planned;
          const ImVec4 col = st == CommandStatus::Implemented ? ImVec4(ThemeColors::kOk[0], ThemeColors::kOk[1], ThemeColors::kOk[2], 1) :
                             st == CommandStatus::Partial ? ImVec4(ThemeColors::kWarn[0], ThemeColors::kWarn[1], ThemeColors::kWarn[2], 1) :
                             ImVec4(ThemeColors::kMuted[0], ThemeColors::kMuted[1], ThemeColors::kMuted[2], 1);
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::PushID(i);
          const ImVec2 p = ImGui::GetCursorScreenPos();
          const float ih = ImGui::GetTextLineHeight();
          ImGui::Dummy(ImVec2(ih + 6, ih));
          ImGui::SameLine(0, 0);
          DrawIcon(ImGui::GetWindowDrawList(), c->name.c_str(), p, ih, ImGui::GetColorU32(ImGuiCol_Text), ThemeColors::AccentU32());
          if (ImGui::Selectable(c->name.c_str(), autocomplete_index_ == i, ImGuiSelectableFlags_SpanAllColumns)) {
            command_input_ = c->name;
            autocomplete_index_ = -1;
            focus_command_line_ = true;
          }
          ImGui::PopID();
          ImGui::TableNextColumn();
          StatusBadge(CommandStatusName(st), col);
          ImGui::TableNextColumn();
          std::string desc = c->description;
          const size_t nl = desc.find('\n');
          if (nl != std::string::npos) desc.erase(nl);
          if (desc.size() > 52) desc = desc.substr(0, 49) + "...";
          ImGui::TextDisabled("%s", desc.c_str());
        }
        ImGui::EndTable();
      }
      ImGui::TextDisabled("Up/Down select   Tab complete   Enter run");
      ImGui::End();
      ImGui::PopStyleVar(2);
      // Tab completes to the highlighted (or first) match.
      if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        const int pick = autocomplete_index_ >= 0 ? autocomplete_index_ : 0;
        command_input_ = matches[static_cast<size_t>(pick)]->name;
        autocomplete_index_ = -1;
        focus_command_line_ = true;
      }
    }
  }
  if (!showing) { autocomplete_count_ = 0; autocomplete_index_ = -1; autocomplete_prefix_.clear(); }
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
      ImGui::PushStyleColor(ImGuiCol_Button, value ? ThemeColors::Accent(0.85f) : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, value ? ThemeColors::Accent(1.0f) : ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered]);
      ImGui::PushStyleColor(ImGuiCol_Text, value ? ImVec4(1, 1, 1, 1) : ImGui::GetStyle().Colors[ImGuiCol_Text]);
      if (ImGui::SmallButton(label)) value = !value;
      ImGui::PopStyleColor(3);
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
  // Cursor coordinates (live), units, current layer, snap toggles, hint, bell.
  ImDrawList* dl = ImGui::GetWindowDrawList();
  {
    const std::string coords = pending_hover_ ? FormatPoint(*pending_hover_) : std::string("x, y, z");
    ImGui::PushStyleColor(ImGuiCol_Text, pending_hover_ ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const float w = std::max(150.0f, ImGui::CalcTextSize(coords.c_str()).x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(coords.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(0, w - ImGui::CalcTextSize(coords.c_str()).x + 12);
  }
  ImGui::TextDisabled("%s", doc_.Settings().unit_system.c_str());
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Document units (Document Properties > Units)");
  ImGui::SameLine(0, 14);
  // Current layer: swatch + combo to change it.
  {
    const int cur = doc_.CurrentLayer();
    const Layer& layer = doc_.Layers()[static_cast<size_t>(cur)];
    ImGui::ColorButton("##curlayer", ImVec4(layer.color.r, layer.color.g, layer.color.b, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::CalcTextSize(doc_.LayerFullPath(cur).c_str()).x + 36.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 1));
    if (ImGui::BeginCombo("##layercombo", doc_.LayerFullPath(cur).c_str(), ImGuiComboFlags_HeightLarge)) {
      for (size_t i = 0; i < doc_.Layers().size(); ++i) {
        const Layer& l = doc_.Layers()[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::ColorButton("##c", ImVec4(l.color.r, l.color.g, l.color.b, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
        ImGui::SameLine();
        if (ImGui::Selectable(doc_.LayerFullPath(static_cast<int>(i)).c_str(), static_cast<int>(i) == cur)) doc_.SetCurrentLayer(static_cast<int>(i));
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Current layer: new objects go here. Click to change.");
  }
  ImGui::SameLine(0, 14);
  // Snap toggles.
  auto toggle = [&](const char* label, bool& value, const char* tip) {
    ImGui::PushStyleColor(ImGuiCol_Button, value ? ThemeColors::Accent(0.85f) : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, value ? ThemeColors::Accent(1.0f) : ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered]);
    ImGui::PushStyleColor(ImGuiCol_Text, value ? ImVec4(1, 1, 1, 1) : ImGui::GetStyle().Colors[ImGuiCol_Text]);
    if (ImGui::SmallButton(label)) value = !value;
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(0, 4);
  };
  toggle("Grid Snap", snaps_.grid_snap, "Snap picked points to the grid (F9)");
  toggle("Ortho", snaps_.ortho, "Constrain to 90 degree angles (F8)");
  toggle("Planar", snaps_.planar, "Keep picks at the elevation of the previous point");
  toggle("Osnap", panels_.object_snaps, "Show the object snap toolbar");
  toggle("SmartTrack", snaps_.smart_track, "SmartTrack tracking lines");
  toggle("Gumball", gumball_enabled, "Drag selected objects with the on-screen gumball");
  ImGui::SameLine(0, 14);
  ImGui::TextDisabled("%zu objects, %zu selected", doc_.ObjectCount(), doc_.SelectedCount());
  if (doc_.Modified()) {
    ImGui::SameLine(0, 8);
    ImGui::TextDisabled("(modified)");
  }
  ImGui::SameLine(0, 14);
  // Right side: active-command hint and the notifications bell.
  {
    const float bell_w = 30.0f;
    std::string hint;
    if (engine_->IsRunning()) hint = engine_->ActiveName() + ": " + engine_->Prompt();
    else if (!engine_->History().empty()) hint = engine_->History().back();
    const float avail = ImGui::GetWindowWidth() - ImGui::GetCursorPosX() - bell_w - 24.0f;
    if (!hint.empty() && avail > 120.0f) {
      float hw = ImGui::CalcTextSize(hint.c_str()).x;
      while (hw > avail && hint.size() > 8) { hint = hint.substr(0, hint.size() - 4) + "..."; hw = ImGui::CalcTextSize(hint.c_str()).x; }
      ImGui::SameLine(ImGui::GetWindowWidth() - bell_w - 16.0f - hw);
      if (engine_->IsRunning()) ImGui::TextColored(ThemeColors::Accent(), "%s", hint.c_str());
      else ImGui::TextDisabled("%s", hint.c_str());
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", engine_->IsRunning() ? "The active command's prompt" : "Last command-line message (F2 for the full history)");
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - bell_w - 6.0f);
    const ImVec2 bp = ImGui::GetCursorScreenPos();
    const float bh = ImGui::GetFrameHeight();
    if (ImGui::InvisibleButton("##bell", ImVec2(bell_w, bh))) {
      panels_.notifications = !panels_.notifications;
      unread_notifications = 0;
    }
    const bool bell_hover = ImGui::IsItemHovered();
    if (bell_hover) {
      ImGui::SetTooltip("Notifications (%d unread)", unread_notifications);
      dl->AddRectFilled(bp, ImVec2(bp.x + bell_w, bp.y + bh), ImGui::GetColorU32(ImGuiCol_ButtonHovered), 4.0f);
    }
    // Bell glyph.
    const ImU32 bc = ImGui::GetColorU32(unread_notifications > 0 ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const float cx = bp.x + bell_w * 0.5f, cy = bp.y + bh * 0.5f;
    const float r = bh * 0.26f;
    dl->PathArcTo(ImVec2(cx, cy - r * 0.2f), r, 3.14159f, 6.28318f);
    dl->PathLineTo(ImVec2(cx + r, cy + r * 0.8f));
    dl->PathLineTo(ImVec2(cx + r * 1.3f, cy + r * 1.1f));
    dl->PathLineTo(ImVec2(cx - r * 1.3f, cy + r * 1.1f));
    dl->PathLineTo(ImVec2(cx - r, cy + r * 0.8f));
    dl->PathStroke(bc, ImDrawFlags_Closed, 1.5f);
    dl->AddCircleFilled(ImVec2(cx, cy + r * 1.5f), r * 0.32f, bc);
    if (unread_notifications > 0) {
      const std::string n = std::to_string(std::min(unread_notifications, 99));
      const ImVec2 ts = ImGui::CalcTextSize(n.c_str());
      const ImVec2 c(cx + r * 1.2f, cy - r * 0.9f);
      dl->AddCircleFilled(c, std::max(ts.x, ts.y) * 0.62f, IM_COL32(225, 70, 70, 255));
      dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32_WHITE, n.c_str());
    }
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
  if (panels_.toolbars) { DrawToolbars(*this); DrawLeftSidebar(*this); }
  if (panels_.layers) DrawLayersPanel(*this);
  if (panels_.properties) DrawPropertiesPanel(*this);
  if (panels_.command_history) DrawCommandHistoryPanel(*this);
  if (panels_.command_list) DrawCommandListPanel(*this, command_list_filter_, command_list_status_filter_);
  if (panels_.help) DrawHelpPanel(*this, help_search_);
  if (panels_.notifications) { unread_notifications = 0; DrawNotificationsPanel(*this); }
  if (panels_.named_views) DrawNamedViewsPanel(*this);
  if (panels_.notes) DrawNotesPanel(*this, notes_buffer_, sizeof(notes_buffer_));
  if (panels_.document_user_text) DrawDocumentUserTextPanel(*this);
  if (panels_.materials) DrawMaterialsPanel(*this);
  if (panels_.lights) DrawLightsPanel(*this);
  if (panels_.rendering) DrawRenderingPanel(*this);
  if (panels_.environments) DrawEnvironmentsPanel(*this);
  if (panels_.textures) DrawTexturesPanel(*this);
  if (panels_.render_window) DrawRenderWindow(*this);
  if (panels_.display) DrawDisplayPanel(*this);
  if (panels_.calculator) DrawCalculatorPanel(*this, calc_input_, calc_result_);
  if (panels_.about) DrawAboutWindow(*this);
  if (panels_.options) DrawOptionsWindow(*this);
  if (panels_.document_properties) DrawDocumentPropertiesWindow(*this);
  if (panels_.linetypes) DrawLinetypesPanel(*this);
  if (panels_.box_edit) DrawBoxEditPanel(*this);
  if (panels_.undo_multiple) DrawUndoMultipleWindow(*this, false);
  if (panels_.redo_multiple) DrawUndoMultipleWindow(*this, true);
  if (panels_.layer_state_manager) DrawLayerStateManager(*this);
  if (panels_.selection_filter) DrawSelectionFilterPanel(*this);
  if (panels_.macro_editor) DrawMacroEditor(*this);
  if (panels_.clipping_planes) DrawClippingPlanesPanel(*this);
  if (panels_.layouts) DrawLayoutsPanel(*this);
  if (panels_.named_cplanes) DrawNamedCPlanesPanel(*this);
  if (panels_.imgui_demo) ImGui::ShowDemoWindow(&panels_.imgui_demo);
}

}  // namespace dino8::app
