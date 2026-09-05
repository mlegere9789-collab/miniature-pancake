// The Dino 8 application object: owns the document, the viewports, the
// command engine, the renderer and all UI state, and runs one frame at a
// time from main().
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "commands/CommandCatalog.h"
#include "commands/CommandEngine.h"
#include "doc/Document.h"
#include "render/GlRenderer.h"
#include "ui/Gumball.h"
#include "imgui.h"
#include "viewport/Viewport.h"

namespace dino8::app {

struct PanelState {
  bool layers = true;
  bool properties = true;
  bool command_history = true;
  bool command_list = false;
  bool help = false;
  bool notifications = false;
  bool named_views = false;
  bool notes = false;
  bool document_user_text = false;
  bool materials = false;
  bool display = false;
  bool object_snaps = true;
  bool toolbars = true;
  bool calculator = false;
  bool imgui_demo = false;
  bool about = false;
  bool options = false;
  bool box_edit = false;
  bool undo_multiple = false;
  bool redo_multiple = false;
  bool layer_state_manager = false;
  bool selection_filter = false;
  bool macro_editor = false;
  bool document_properties = false;
  bool lights = false;
  bool rendering = false;
  bool environments = false;
  bool textures = false;
  bool render_window = false;
  bool linetypes = false;
};

// The last image produced by Render / RenderPreview, shown in the Render
// Window panel and written by SaveRenderWindowAs.
struct RenderImage {
  int width = 0, height = 0;
  std::vector<unsigned char> rgb;  // top-down rows
  unsigned texture = 0;            // GL texture for the panel (owned)
  std::string view_name;
  std::string last_saved_path;
  double seconds = 0;
  bool Valid() const { return width > 0 && height > 0 && rgb.size() == static_cast<size_t>(width) * height * 3; }
};

// Small app-wide switches set by commands (SelectionFilter*, Echo, DragMode,
// SetRedrawOff, SetWorkingFolder...). Plain data so cmd_state.cpp and
// cmd_select2.cpp can read and toggle them without UI code.
struct AppState {
  // Sub-object selection filter (SelectionFilterEdges/Faces/Vertices/...).
  bool filter_enabled = true;
  bool filter_edges = false, filter_faces = false, filter_vertices = false;
  bool echo = true;              // Echo / NoEcho: print script commands to the history
  bool redraw = true;            // SetRedrawOn / SetRedrawOff
  bool command_prompt = true;    // CommandPrompt / DisplayCommandPrompt
  bool viewport_tabs = false;    // ViewportTabs
  bool toolbar_lock = false;     // ToolbarLock
  bool left_sidebar = true, right_sidebar = true;
  bool alerter = false;          // Alerter: beep when a command finishes
  std::string drag_mode = "CPlane";  // DragMode: CPlane / World / UVN / View / ControlPolygon
  double drag_strength = 100.0;  // DragStrength percent
  bool drag_copy = false;        // DragCopy
  bool remember_copy_options = false;
  double ortho_angle = 90.0;     // OrthoAngle degrees
  bool ortho_snap_to_cplane_z = false;
  bool snap_to_locked = true, snap_to_occluded = true, snap_to_meshes = true, snap_to_mesh_object = true, snap_to_subd_object = true;
  double zoom_extents_border = 1.1;  // SetZoomExtentsBorder factor
  double perspective_angle = 0;      // PerspectiveAngle (0 = derived from the lens)
  bool lock_viewport = false;        // LockViewport: ignore view changes from the mouse
  std::string working_folder;        // SetWorkingFolder
  bool message_boxes_reset = false;
  bool camera_shown = false;         // Camera Show: draw the active camera frustum
};

struct FileDialogState {
  bool open = false;
  bool save = false;
  std::string title;
  std::vector<std::string> extensions;  // e.g. {".3dm"}
  std::string directory;
  std::string filename;
  std::function<void(const std::string&)> callback;
};

struct Notification {
  std::string text;
  double time = 0;
};

class Application {
 public:
  Application();
  ~Application();

  bool Init(const std::string& exe_dir, std::string& error);
  void Shutdown();
  void Frame();  // one full UI + render frame

  // ---- accessors used by commands and panels ----
  Document& Doc() { return doc_; }
  CommandEngine& Engine() { return *engine_; }
  CommandCatalog& Catalog() { return catalog_; }
  std::vector<std::unique_ptr<Viewport>>& Viewports() { return viewports_; }
  Viewport* ActiveViewport();
  Viewport* FindViewport(const std::string& name);
  SnapSettings& Snaps() { return snaps_; }
  PanelState& Panels() { return panels_; }
  AppState& State() { return state_; }
  Gumball& GetGumball() { return gumball_; }
  // The GLFW window (as an opaque pointer so headers stay GLFW-free); set by
  // main() so Fullscreen/Maximize/Minimize/Restore can drive the OS window.
  void* native_window = nullptr;
  // True in --smoke runs (hidden window): commands must not open browsers
  // or minimise the window.
  bool headless = false;
  bool WantsQuit() const { return quit_; }
  // Asks "Save changes?" when the document is modified, then runs `then`.
  void ConfirmDiscard(std::function<void()> then);
  void RequestQuit() { ConfirmDiscard([this]() { quit_ = true; }); }
  const std::string& ExeDir() const { return exe_dir_; }

  void ShowHelpFor(const std::string& command_name);
  void ZoomExtentsAll();
  void SetViewportLayout(int count);  // 1, 3 or 4
  void FocusCommandLine() { focus_command_line_ = true; }
  void Notify(const std::string& text);
  void ShowFileDialog(const std::string& title, const std::vector<std::string>& extensions, bool save,
                      std::function<void(const std::string&)> callback);
  std::vector<std::string>& RecentFiles() { return recent_files_; }
  void AddRecentFile(const std::string& path);
  float ui_scale = 1.0f;
  bool gumball_enabled = true;
  bool light_theme = false;
  std::vector<std::string> toolbar_commands;  // customizable toolbar (empty = default set)
  // True when a saved ImGui layout exists, so the default dock layout is not rebuilt over it.
  bool has_saved_layout = false;

  // Document-level operations used by both menus and commands.
  bool NewDocument(bool confirm_discard);
  bool OpenDocument(const std::string& path, std::string& error);
  bool SaveDocument(const std::string& path, std::string& error);
  bool ImportFile(const std::string& path, std::string& error);
  bool ExportSelected(const std::string& path, std::string& error);
  // Vector line-art output (.svg / .pdf) of the active view. `scale` = page
  // millimetres per document unit, 0 = fit to page.
  bool ExportDrawing(const std::string& path, bool selected_only, double scale, std::string& error);

  // Built-in renderer: renders `vp` (the active viewport when null) in
  // Rendered mode into the render window image. `supersample` <= 0 uses
  // the document's render quality; `arctic` renders white matte.
  bool RenderView(Viewport* vp, int width, int height, int supersample, bool arctic, std::string& error);
  RenderImage& LastRender() { return last_render_; }
  bool SaveLastRender(const std::string& path, std::string& error);
  void CloseRenderWindow();
  GlRenderer& Renderer() { return renderer_; }
  Viewport::FrameContext MakeFrameContext();

  // Surface analysis (Zebra / EMap / CurvatureAnalysis / DraftAngleAnalysis):
  // `analysis_defaults` remembers the options between command runs, and
  // `analysis_fallback` is the app-wide mode applied to every surface that
  // has no per-object analysis (set by running a command with nothing selected).
  AnalysisSettings analysis_defaults;
  AnalysisSettings analysis_fallback;

  // Display settings.
  bool show_control_points_for_selected = false;
  double curve_display_tolerance = 0.02;
  double surface_display_tolerance = 0.05;
  std::string help_command;  // command whose help the Help panel shows

 private:
  void RegisterCommands();
  void DrawDockspace();
  void DrawViewports();
  void DrawPanels();
  void DrawCommandLine();
  void DrawStatusBar();
  float StatusBarHeight() const;
  void DrawFileDialog();
  void DrawNotifications();
  void DrawConfirmDiscard();
  void DrawPopupToolbar();
  void HandleShortcuts();
  void ProcessViewportEvents(Viewport& vp, const ViewportEvents& ev);
  void BuildDefaultLayout(unsigned dockspace_id);

  std::string exe_dir_;
  Document doc_;
  CommandCatalog catalog_;
  std::unique_ptr<CommandEngine> engine_;
  Gumball gumball_;
  GlRenderer renderer_;
  std::vector<std::unique_ptr<Viewport>> viewports_;
  int active_viewport_ = 3;
  SnapSettings snaps_;
  PanelState panels_;
  AppState state_;
  FileDialogState file_dialog_;
  std::deque<Notification> notifications_;
  std::vector<std::string> recent_files_;
  std::string command_input_;
  std::vector<std::string> command_line_history_;
  int history_cursor_ = -1;
  bool focus_command_line_ = true;
  int focus_retries_ = 0;
  bool quit_ = false;
  bool layout_built_ = false;
  bool renderer_ok_ = false;
  std::string renderer_error_;
  std::optional<kernel::Point3d> pending_hover_;
  std::string command_list_filter_;
  int command_list_status_filter_ = 0;  // 0 all, 1 implemented, 2 partial, 3 planned
  std::string help_search_;
  int selected_layer_row_ = -1;
  char rename_buffer_[128] = {};
  char layer_name_buffer_[128] = {};
  char notes_buffer_[8192] = {};
  std::string calc_input_;
  std::string calc_result_;
  std::string last_saved_path_;
  std::function<void()> pending_after_confirm_;
  bool confirm_open_ = false;
  bool open_popup_toolbar_ = false;
  ImVec2 popup_toolbar_pos_;
  RenderImage last_render_;
};

}  // namespace dino8::app
