#include "app/Settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "app/Application.h"
#include "util/json_mini.h"

namespace dino8::app {

namespace fs = std::filesystem;

std::string ConfigDirectory() {
  fs::path dir;
#if defined(_WIN32)
  if (const char* appdata = std::getenv("APPDATA")) dir = fs::path(appdata) / "Dino8";
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME")) dir = fs::path(home) / "Library" / "Application Support" / "Dino8";
#else
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) dir = fs::path(xdg) / "dino8";
  else if (const char* home = std::getenv("HOME")) dir = fs::path(home) / ".config" / "dino8";
#endif
  if (dir.empty()) dir = fs::temp_directory_path() / "dino8";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir.string();
}

namespace {

std::string Escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      default: out += c;
    }
  }
  return out;
}

double Num(const json::Value& v, double fallback) {
  if (v.IsString()) return std::atof(v.string.c_str());
  return v.type == json::Value::Type::Number ? v.number : fallback;
}

bool Bool(const json::Value& v, bool fallback) {
  if (v.type == json::Value::Type::Bool) return v.boolean;
  return fallback;
}

}  // namespace

bool LoadSettings(Application& app, float& ui_scale) {
  return LoadSettingsFrom((fs::path(ConfigDirectory()) / "settings.json").string(), app, ui_scale);
}

bool SaveSettings(const Application& app, float ui_scale) {
  return SaveSettingsTo((fs::path(ConfigDirectory()) / "settings.json").string(), app, ui_scale);
}

bool LoadSettingsFrom(const std::string& path_str, Application& app, float& ui_scale) {
  const fs::path path = path_str;
  std::ifstream in(path);
  if (!in) return false;
  std::stringstream buf;
  buf << in.rdbuf();
  json::Value root;
  std::string err;
  if (!json::Parse(buf.str(), root, err) || !root.IsObject()) return false;

  ui_scale = static_cast<float>(Num(root["ui_scale"], ui_scale));
  const json::Value& recent = root["recent_files"];
  app.RecentFiles().clear();
  for (size_t i = 0; i < recent.Size() && i < 10; ++i) app.RecentFiles().push_back(recent[i].AsString());

  SnapSettings& s = app.Snaps();
  const json::Value& snaps = root["snaps"];
  s.end = Bool(snaps["end"], s.end); s.mid = Bool(snaps["mid"], s.mid); s.cen = Bool(snaps["cen"], s.cen);
  s.point = Bool(snaps["point"], s.point); s.near_ = Bool(snaps["near"], s.near_); s.vertex = Bool(snaps["vertex"], s.vertex);
  s.int_ = Bool(snaps["int"], s.int_); s.perp = Bool(snaps["perp"], s.perp); s.tan = Bool(snaps["tan"], s.tan);
  s.quad = Bool(snaps["quad"], s.quad); s.grid_snap = Bool(snaps["grid_snap"], s.grid_snap);
  s.ortho = Bool(snaps["ortho"], s.ortho); s.planar = Bool(snaps["planar"], s.planar); s.smart_track = Bool(snaps["smart_track"], s.smart_track);

  PanelState& p = app.Panels();
  const json::Value& panels = root["panels"];
  p.layers = Bool(panels["layers"], p.layers); p.properties = Bool(panels["properties"], p.properties);
  p.command_history = Bool(panels["command_history"], p.command_history); p.command_list = Bool(panels["command_list"], p.command_list);
  p.help = Bool(panels["help"], p.help); p.named_views = Bool(panels["named_views"], p.named_views);
  p.notes = Bool(panels["notes"], p.notes); p.materials = Bool(panels["materials"], p.materials);
  p.display = Bool(panels["display"], p.display); p.object_snaps = Bool(panels["object_snaps"], p.object_snaps);
  p.toolbars = Bool(panels["toolbars"], p.toolbars);

  app.light_theme = Bool(root["light_theme"], app.light_theme);
  app.gumball_enabled = Bool(root["gumball"], app.gumball_enabled);
  const json::Value& tb = root["toolbar"];
  if (tb.IsArray() && tb.Size() > 0) { app.toolbar_commands.clear(); for (size_t i = 0; i < tb.Size(); ++i) app.toolbar_commands.push_back(tb[i].AsString()); }
  if (root["working_folder"].IsString()) app.State().working_folder = root["working_folder"].AsString();
  app.curve_display_tolerance = Num(root["curve_display_tolerance"], app.curve_display_tolerance);
  app.surface_display_tolerance = Num(root["surface_display_tolerance"], app.surface_display_tolerance);
  return true;
}

bool SaveSettingsTo(const std::string& path_str, const Application& app, float ui_scale) {
  Application& a = const_cast<Application&>(app);
  const fs::path path = path_str;
  std::ofstream out(path);
  if (!out) return false;
  out << "{\n";
  out << "  \"ui_scale\": " << ui_scale << ",\n";
  out << "  \"light_theme\": " << (a.light_theme ? "true" : "false") << ",\n";
  out << "  \"gumball\": " << (a.gumball_enabled ? "true" : "false") << ",\n";
  out << "  \"toolbar\": [";
  for (size_t i = 0; i < a.toolbar_commands.size(); ++i) out << (i ? ", " : "") << "\"" << Escape(a.toolbar_commands[i]) << "\"";
  out << "],\n";
  out << "  \"working_folder\": \"" << Escape(a.State().working_folder) << "\",\n";
  out << "  \"curve_display_tolerance\": " << a.curve_display_tolerance << ",\n";
  out << "  \"surface_display_tolerance\": " << a.surface_display_tolerance << ",\n";
  out << "  \"recent_files\": [";
  for (size_t i = 0; i < a.RecentFiles().size(); ++i) out << (i ? ", " : "") << "\"" << Escape(a.RecentFiles()[i]) << "\"";
  out << "],\n";
  const SnapSettings& s = a.Snaps();
  auto b = [](bool v) { return v ? "true" : "false"; };
  out << "  \"snaps\": {\"end\": " << b(s.end) << ", \"mid\": " << b(s.mid) << ", \"cen\": " << b(s.cen) << ", \"point\": " << b(s.point)
      << ", \"near\": " << b(s.near_) << ", \"vertex\": " << b(s.vertex) << ", \"int\": " << b(s.int_) << ", \"perp\": " << b(s.perp)
      << ", \"tan\": " << b(s.tan) << ", \"quad\": " << b(s.quad) << ", \"grid_snap\": " << b(s.grid_snap) << ", \"ortho\": " << b(s.ortho)
      << ", \"planar\": " << b(s.planar) << ", \"smart_track\": " << b(s.smart_track) << "},\n";
  const PanelState& p = a.Panels();
  out << "  \"panels\": {\"layers\": " << b(p.layers) << ", \"properties\": " << b(p.properties) << ", \"command_history\": " << b(p.command_history)
      << ", \"command_list\": " << b(p.command_list) << ", \"help\": " << b(p.help) << ", \"named_views\": " << b(p.named_views)
      << ", \"notes\": " << b(p.notes) << ", \"materials\": " << b(p.materials) << ", \"display\": " << b(p.display)
      << ", \"object_snaps\": " << b(p.object_snaps) << ", \"toolbars\": " << b(p.toolbars) << "}\n";
  out << "}\n";
  return true;
}

}  // namespace dino8::app
