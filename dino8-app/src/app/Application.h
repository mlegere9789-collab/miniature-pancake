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
};

}  // namespace dino8::app
