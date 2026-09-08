#include "plugins/PluginManager.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "app/Application.h"
#include "app/Settings.h"
#include "commands/Command.h"
#include "commands/CommandEngine.h"
#include "commands/cmd_common.h"
#include "dino8_plugin.h"
#include "flow/FlowGraph.h"
#include "util/json_mini.h"

namespace dino8::plugins {

namespace fs = std::filesystem;
using app::Application;
using app::CommandContext;

namespace {

#if defined(_WIN32)
void* DoOpen(const std::string& path) { return reinterpret_cast<void*>(LoadLibraryA(path.c_str())); }
void* DoSymbol(void* h, const char* name) { return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(h), name)); }
void DoClose(void* h) { if (h) FreeLibrary(reinterpret_cast<HMODULE>(h)); }
std::string DoError() { return "LoadLibrary failed (error " + std::to_string(GetLastError()) + ")"; }
const char* kLibExt = ".dll";
#else
void* DoOpen(const std::string& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* DoSymbol(void* h, const char* name) { return dlsym(h, name); }
void DoClose(void* h) { if (h) dlclose(h); }
std::string DoError() { const char* e = dlerror(); return e ? e : "dlopen failed"; }
#if defined(__APPLE__)
const char* kLibExt = ".dylib";
#else
const char* kLibExt = ".so";
#endif
#endif

// Only one Application exists per process, so the plain C function pointers
// the ABI requires can reach it through a process-wide pointer rather than
// per-call context threading.
Application* g_app = nullptr;
LoadedPlugin* g_loading = nullptr;  // the plug-in whose init() is currently running

struct PluginCommandFn {
  Dino8CommandFn fn;
  void* user_data;
  std::string name;
};

// A command line command backed by a plug-in's callback: it takes whatever
// tokens were already queued on the command line as argv and runs
// immediately (plug-in commands are not interactive prompts).
class PluginCommand : public app::Command {
 public:
  explicit PluginCommand(PluginCommandFn spec) : spec_(std::move(spec)) {}
  void Begin(CommandContext& ctx) override {
    std::vector<std::string> argv_storage;
    argv_storage.push_back(spec_.name);
    while (auto tok = ctx.Engine().TakePendingInput()) argv_storage.push_back(*tok);
    std::vector<const char*> argv;
    for (const std::string& s : argv_storage) argv.push_back(s.c_str());
    const int rc = spec_.fn(static_cast<int>(argv.size()), argv.data(), reinterpret_cast<Dino8CommandContext*>(&ctx));
    if (rc != 0) ctx.Warn(spec_.name + ": plug-in command returned error code " + std::to_string(rc));
    Finish();
  }

 private:
  PluginCommandFn spec_;
};

void ApiPrint(const char* text) {
  if (!g_app || !text) return;
  g_app->Engine().Print(text);
}

int ApiRegisterCommand(const char* name, const char* help, Dino8CommandFn fn, void* user_data) {
  if (!g_app || !name || !fn) return 1;
  PluginCommandFn spec{fn, user_data, name};
  app::CommandFactory factory = [spec]() -> std::unique_ptr<app::Command> { return std::make_unique<PluginCommand>(spec); };
  g_app->Engine().Register(name, factory, app::CommandStatus::Implemented, help ? help : "");
  if (g_loading) g_loading->commands.emplace_back(name);
  return 0;
}

int ApiRunCommand(const char* command_line) {
  if (!g_app || !command_line) return 1;
  g_app->Engine().Execute(command_line);
  return 0;
}

unsigned long long ApiAddPoint(double x, double y, double z) {
  if (!g_app) return 0;
  app::Document& doc = g_app->Doc();
  doc.BeginChange("Plug-in: AddPoint");
  return doc.Add(app::SceneObject::MakePoint(kernel::Point3d(x, y, z)));
}

unsigned long long ApiAddLine(double x0, double y0, double z0, double x1, double y1, double z1) {
  if (!g_app) return 0;
  ON_LineCurve lc(kernel::Point3d(x0, y0, z0), kernel::Point3d(x1, y1, z1));
  ON_NurbsCurve nc;
  lc.GetNurbForm(nc);
  kernel::NurbsCurve c;
  c.raw() = nc;
  app::Document& doc = g_app->Doc();
  doc.BeginChange("Plug-in: AddLine");
  return doc.Add(app::SceneObject::MakeCurve(c));
}

unsigned long long ApiAddPolyline(const double* xyz, int point_count, int closed) {
  if (!g_app || !xyz || point_count < 2) return 0;
  ON_Polyline pl;
  for (int i = 0; i < point_count; ++i) pl.Append(ON_3dPoint(xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]));
  if (closed && pl.Count() > 0 && pl[0].DistanceTo(pl[pl.Count() - 1]) > 1e-9) pl.Append(pl[0]);
  ON_PolylineCurve pc(pl);
  ON_NurbsCurve nc;
  pc.GetNurbForm(nc);
  kernel::NurbsCurve c;
  c.raw() = nc;
  app::Document& doc = g_app->Doc();
  doc.BeginChange("Plug-in: AddPolyline");
  return doc.Add(app::SceneObject::MakeCurve(c));
}

unsigned long long ApiAddMesh(const double* xyz, int vertex_count, const int* faces, int face_count) {
  if (!g_app || !xyz || vertex_count <= 0) return 0;
  kernel::Mesh m;
  ON_Mesh& raw = m.raw();
  for (int i = 0; i < vertex_count; ++i) raw.SetVertex(i, ON_3dPoint(xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]));
  for (int f = 0; f < face_count; ++f) raw.SetQuad(f, faces[f * 4], faces[f * 4 + 1], faces[f * 4 + 2], faces[f * 4 + 3]);
  raw.ComputeFaceNormals();
  raw.ComputeVertexNormals();
  app::Document& doc = g_app->Doc();
  doc.BeginChange("Plug-in: AddMesh");
  return doc.Add(app::SceneObject::MakeMesh(m));
}

int ApiGetSelection(unsigned long long* out, int max) {
  if (!g_app) return 0;
  std::vector<app::ObjectId> sel = g_app->Doc().SelectedIds();
  for (int i = 0; i < static_cast<int>(sel.size()) && i < max; ++i) out[i] = sel[static_cast<size_t>(i)];
  return static_cast<int>(sel.size());
}

int ApiGetObjectBBox(unsigned long long id, double* min_xyz, double* max_xyz) {
  if (!g_app) return 0;
  const app::SceneObject* o = g_app->Doc().Find(static_cast<app::ObjectId>(id));
  if (!o) return 0;
  const kernel::BoundingBox bb = o->BoundingBox();
  min_xyz[0] = bb.min.x; min_xyz[1] = bb.min.y; min_xyz[2] = bb.min.z;
  max_xyz[0] = bb.max.x; max_xyz[1] = bb.max.y; max_xyz[2] = bb.max.z;
  return 1;
}

int ApiDeleteObject(unsigned long long id) {
  if (!g_app) return 0;
  g_app->Doc().BeginChange("Plug-in: Delete");
  return g_app->Doc().Remove(static_cast<app::ObjectId>(id)) ? 1 : 0;
}

int ApiSelectObject(unsigned long long id, int selected) {
  if (!g_app) return 0;
  if (!g_app->Doc().Find(static_cast<app::ObjectId>(id))) return 0;
  g_app->Doc().Select(static_cast<app::ObjectId>(id), selected != 0);
  return 1;
}

int ApiSetUserText(unsigned long long id, const char* key, const char* value) {
  if (!g_app || !key) return 0;
  app::SceneObject* o = g_app->Doc().Find(static_cast<app::ObjectId>(id));
  if (!o) return 0;
  g_app->Doc().BeginChange("Plug-in: SetUserText");
  if (value) o->user_text[key] = value; else o->user_text.erase(key);
  return 1;
}

// ---- Geometry handles ----------------------------------------------------
// The plug-in ABI never exposes a real kernel::NurbsCurve/Mesh/etc pointer -
// that would tie every plug-in binary to this build's exact OpenNURBS-backed
// layout. Instead a CURVE/SURFACE/BREP/MESH Dino8FlowValue carries a plain
// integer handle into this table, which holds the actual flow::Value (and
// therefore its shared_ptr geometry) on Dino 8's side.
//
// Lifetime: a handle is populated right before a plug-in node's evaluator
// runs (for its input ports) or created by the plug-in during the call (via
// make_curve_value_polyline/make_mesh_value, or by forwarding an input
// handle to an output), and is only ever read back immediately after that
// same call returns (ApiRegisterFlowNode's eval lambda, below). The table is
// cleared after every evaluator call, so handles never outlive the call
// that produced them - matching the "valid only during the call" contract
// already used for Dino8CommandContext.
std::unordered_map<unsigned long long, flow::Value> g_geom_handles;
unsigned long long g_next_geom_handle = 1;

unsigned long long PutGeom(flow::Value v) {
  const unsigned long long h = g_next_geom_handle++;
  g_geom_handles.emplace(h, std::move(v));
  return h;
}

const flow::Value* GetGeom(unsigned long long h) {
  auto it = g_geom_handles.find(h);
  return it == g_geom_handles.end() ? nullptr : &it->second;
}

void ClearGeomHandles() { g_geom_handles.clear(); }

double ApiCurveLength(unsigned long long geom) {
  const flow::Value* v = GetGeom(geom);
  return (v && v->kind == flow::Kind::Curve && v->curve) ? v->curve->Length(400) : 0.0;
}

int ApiMeshFaceCount(unsigned long long geom) {
  const flow::Value* v = GetGeom(geom);
  return (v && v->kind == flow::Kind::Mesh && v->mesh) ? v->mesh->FaceCount() : 0;
}

int ApiMeshVertexCount(unsigned long long geom) {
  const flow::Value* v = GetGeom(geom);
  return (v && v->kind == flow::Kind::Mesh && v->mesh) ? v->mesh->VertexCount() : 0;
}

unsigned long long ApiMakeCurveValuePolyline(const double* xyz, int point_count, int closed) {
  if (!xyz || point_count < 2) return 0;
  ON_Polyline pl;
  for (int i = 0; i < point_count; ++i) pl.Append(ON_3dPoint(xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]));
  if (closed && pl.Count() > 0 && pl[0].DistanceTo(pl[pl.Count() - 1]) > 1e-9) pl.Append(pl[0]);
  ON_PolylineCurve pc(pl);
  ON_NurbsCurve nc;
  pc.GetNurbForm(nc);
  kernel::NurbsCurve c;
  c.raw() = nc;
  return PutGeom(flow::Value::Curve(std::move(c)));
}

unsigned long long ApiMakeMeshValue(const double* xyz, int vertex_count, const int* faces, int face_count) {
  if (!xyz || vertex_count <= 0) return 0;
  kernel::Mesh m;
  ON_Mesh& raw = m.raw();
  for (int i = 0; i < vertex_count; ++i) raw.SetVertex(i, ON_3dPoint(xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]));
  for (int f = 0; f < face_count; ++f) raw.SetQuad(f, faces[f * 4], faces[f * 4 + 1], faces[f * 4 + 2], faces[f * 4 + 3]);
  raw.ComputeFaceNormals();
  raw.ComputeVertexNormals();
  return PutGeom(flow::Value::MeshV(std::move(m)));
}

flow::Value FromFlowValue(const Dino8FlowValue& v) {
  switch (v.kind) {
    case DINO8_FLOW_NUMBER: return flow::Value::Number(v.number);
    case DINO8_FLOW_INTEGER: return flow::Value::Integer(static_cast<long long>(v.number));
    case DINO8_FLOW_BOOLEAN: return flow::Value::Boolean(v.number != 0);
    case DINO8_FLOW_TEXT: return flow::Value::Text(std::string(v.text));
    case DINO8_FLOW_POINT: return flow::Value::Point(kernel::Point3d(v.xyz[0], v.xyz[1], v.xyz[2]));
    case DINO8_FLOW_VECTOR: return flow::Value::Vector(kernel::Vector3d(v.xyz[0], v.xyz[1], v.xyz[2]));
    case DINO8_FLOW_CURVE:
    case DINO8_FLOW_SURFACE:
    case DINO8_FLOW_BREP:
    case DINO8_FLOW_MESH: {
      const flow::Value* g = GetGeom(v.geom);
      return g ? *g : flow::Value::Null();
    }
    default: return flow::Value::Null();
  }
}

Dino8FlowValue ToFlowValue(const flow::Value& v) {
  Dino8FlowValue out;
  std::memset(&out, 0, sizeof out);
  switch (v.kind) {
    case flow::Kind::Number: out.kind = DINO8_FLOW_NUMBER; out.number = v.num; break;
    case flow::Kind::Integer: out.kind = DINO8_FLOW_INTEGER; out.number = v.num; break;
    case flow::Kind::Boolean: out.kind = DINO8_FLOW_BOOLEAN; out.number = v.num; break;
    case flow::Kind::Point: out.kind = DINO8_FLOW_POINT; out.xyz[0] = v.point.x; out.xyz[1] = v.point.y; out.xyz[2] = v.point.z; break;
    case flow::Kind::Vector: out.kind = DINO8_FLOW_VECTOR; out.xyz[0] = v.point.x; out.xyz[1] = v.point.y; out.xyz[2] = v.point.z; break;
    case flow::Kind::Curve: out.kind = DINO8_FLOW_CURVE; out.geom = v.curve ? PutGeom(v) : 0; break;
    case flow::Kind::Surface: out.kind = DINO8_FLOW_SURFACE; out.geom = v.surface ? PutGeom(v) : 0; break;
    case flow::Kind::Brep: out.kind = DINO8_FLOW_BREP; out.geom = v.brep ? PutGeom(v) : 0; break;
    case flow::Kind::Mesh: out.kind = DINO8_FLOW_MESH; out.geom = v.mesh ? PutGeom(v) : 0; break;
    default: {
      out.kind = DINO8_FLOW_TEXT;
      const std::string t = v.AsText();
      std::snprintf(out.text, sizeof out.text, "%s", t.c_str());
      break;
    }
  }
  return out;
}

int ApiRegisterFlowNode(const char* name, const char* category, const char* description, const Dino8FlowPort* inputs,
                        int input_count, const Dino8FlowPort* outputs, int output_count, Dino8FlowEvalFn evaluator,
                        void* user_data) {
  if (!name || !evaluator) return 1;
  flow::NodeDef def;
  def.name = name;
  def.nick = name;
  def.category = category && *category ? category : "Plug-ins";
  def.description = description ? description : "";
  def.special = flow::NodeDef::Special::Plugin;
  for (int i = 0; i < input_count; ++i) {
    flow::PortDef p;
    p.name = inputs[i].name ? inputs[i].name : ("In" + std::to_string(i));
    p.nick = p.name;
    switch (inputs[i].kind) {
      case DINO8_FLOW_NUMBER: p.kind = flow::Kind::Number; break;
      case DINO8_FLOW_INTEGER: p.kind = flow::Kind::Integer; break;
      case DINO8_FLOW_BOOLEAN: p.kind = flow::Kind::Boolean; break;
      case DINO8_FLOW_TEXT: p.kind = flow::Kind::Text; break;
      case DINO8_FLOW_POINT: p.kind = flow::Kind::Point; break;
      case DINO8_FLOW_VECTOR: p.kind = flow::Kind::Vector; break;
      case DINO8_FLOW_CURVE: p.kind = flow::Kind::Curve; break;
      case DINO8_FLOW_SURFACE: p.kind = flow::Kind::Surface; break;
      case DINO8_FLOW_BREP: p.kind = flow::Kind::Brep; break;
      case DINO8_FLOW_MESH: p.kind = flow::Kind::Mesh; break;
      default: p.kind = flow::Kind::Any; break;
    }
    if (p.kind == flow::Kind::Number || p.kind == flow::Kind::Integer) p.def = flow::Value::Number(inputs[i].default_number);
    def.inputs.push_back(p);
  }
  for (int i = 0; i < output_count; ++i) {
    flow::PortDef p;
    p.name = outputs[i].name ? outputs[i].name : ("Out" + std::to_string(i));
    p.nick = p.name;
    switch (outputs[i].kind) {
      case DINO8_FLOW_NUMBER: p.kind = flow::Kind::Number; break;
      case DINO8_FLOW_INTEGER: p.kind = flow::Kind::Integer; break;
      case DINO8_FLOW_BOOLEAN: p.kind = flow::Kind::Boolean; break;
      case DINO8_FLOW_TEXT: p.kind = flow::Kind::Text; break;
      case DINO8_FLOW_POINT: p.kind = flow::Kind::Point; break;
      case DINO8_FLOW_VECTOR: p.kind = flow::Kind::Vector; break;
      case DINO8_FLOW_CURVE: p.kind = flow::Kind::Curve; break;
      case DINO8_FLOW_SURFACE: p.kind = flow::Kind::Surface; break;
      case DINO8_FLOW_BREP: p.kind = flow::Kind::Brep; break;
      case DINO8_FLOW_MESH: p.kind = flow::Kind::Mesh; break;
      default: p.kind = flow::Kind::Any; break;
    }
    def.outputs.push_back(p);
  }
  const int in_count = input_count, out_count = output_count;
  def.eval = [evaluator, user_data, in_count, out_count](flow::EvalContext& c) {
    std::vector<Dino8FlowValue> in(static_cast<size_t>(std::max(1, in_count)));
    for (int i = 0; i < in_count; ++i) in[static_cast<size_t>(i)] = ToFlowValue(c.In(i));
    std::vector<Dino8FlowValue> out(static_cast<size_t>(std::max(1, out_count)));
    std::memset(out.data(), 0, out.size() * sizeof(Dino8FlowValue));
    char error[256] = {0};
    const int rc = evaluator(in.data(), in_count, out.data(), out_count, error, static_cast<int>(sizeof error), user_data);
    if (rc != 0) { ClearGeomHandles(); c.Fail(error[0] ? error : "plug-in node evaluation failed"); return; }
    for (int i = 0; i < out_count; ++i) c.Out(i, FromFlowValue(out[static_cast<size_t>(i)]));
    // Handles only need to live for this one call: c.Out() above already
    // copied any geometry it named into the graph's own Tree (via
    // FromFlowValue's shared_ptr copy), so the table can be dropped now
    // rather than accumulating across every solve.
    ClearGeomHandles();
  };
  flow::Registry::Get().Add(def);
  if (g_loading) g_loading->flow_nodes.emplace_back(name);
  return 0;
}

Dino8PluginApi BuildApi(const std::string& config_dir) {
  Dino8PluginApi api;
  std::memset(&api, 0, sizeof api);
  api.api_version = DINO8_PLUGIN_API_VERSION;
  api.app_version = DINO8_VERSION;
  static std::string s_config_dir;
  s_config_dir = config_dir;
  api.config_dir = s_config_dir.c_str();
  api.print = ApiPrint;
  api.register_command = ApiRegisterCommand;
  api.run_command = ApiRunCommand;
  api.add_point = ApiAddPoint;
  api.add_line = ApiAddLine;
  api.add_polyline = ApiAddPolyline;
  api.add_mesh = ApiAddMesh;
  api.get_selection = ApiGetSelection;
  api.get_object_bbox = ApiGetObjectBBox;
  api.delete_object = ApiDeleteObject;
  api.select_object = ApiSelectObject;
  api.set_user_text = ApiSetUserText;
  api.register_flow_node = ApiRegisterFlowNode;
  api.curve_length = ApiCurveLength;
  api.mesh_face_count = ApiMeshFaceCount;
  api.mesh_vertex_count = ApiMeshVertexCount;
  api.make_curve_value_polyline = ApiMakeCurveValuePolyline;
  api.make_mesh_value = ApiMakeMeshValue;
  return api;
}

}  // namespace

Manager& Manager::Get() {
  static Manager m;
  return m;
}

void Manager::ScanDefaultFolders(Application& app) {
  g_app = &app;
  LoadFolder(app, app::ConfigDirectory() + "/plugins");
  LoadFolder(app, app.ExeDir() + "/plugins");
  for (const std::string& f : extra_folders) LoadFolder(app, f);
}

int Manager::LoadFolder(Application& app, const std::string& folder) {
  g_app = &app;
  std::error_code ec;
  if (!fs::exists(folder, ec) || !fs::is_directory(folder, ec)) return 0;
  int count = 0;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != kLibExt) continue;
    const std::string fname = entry.path().filename().string();
    if (std::find(ignored_plugins.begin(), ignored_plugins.end(), fname) != ignored_plugins.end()) continue;
    bool already = false;
    for (const auto& p : plugins_) if (p.path == entry.path().string()) already = true;
    if (already) continue;
    std::string error;
    if (LoadFile(app, entry.path().string(), error)) ++count;
  }
  return count;
}

bool Manager::LoadFile(Application& app, const std::string& path, std::string& error) {
  g_app = &app;
  LoadedPlugin p;
  p.path = path;
  void* handle = DoOpen(path);
  if (!handle) { p.error = error = DoError(); plugins_.push_back(p); return false; }
  p.handle = handle;
  auto init_fn = reinterpret_cast<Dino8PluginInitFn>(DoSymbol(handle, "dino8_plugin_init"));
  if (!init_fn) {
    error = "no dino8_plugin_init() export";
    p.error = error;
    DoClose(handle);
    plugins_.push_back(p);
    return false;
  }
  using NameFn = const char* (*)();
  if (auto name_fn = reinterpret_cast<NameFn>(DoSymbol(handle, "dino8_plugin_name"))) p.name = name_fn();
  if (auto ver_fn = reinterpret_cast<NameFn>(DoSymbol(handle, "dino8_plugin_version"))) p.version = ver_fn();
  if (auto desc_fn = reinterpret_cast<NameFn>(DoSymbol(handle, "dino8_plugin_description"))) p.description = desc_fn();
  if (p.name.empty()) p.name = fs::path(path).stem().string();

  // Plug-ins are expected to keep this pointer for the whole session (they
  // call back through it from command callbacks and node evaluators long
  // after this function returns), so it must not be a stack local - static
  // keeps it alive for the process lifetime rather than dangling once
  // LoadFolder() returns.
  static const Dino8PluginApi api = BuildApi(app::ConfigDirectory());
  g_loading = &p;
  const int rc = init_fn(&api);
  g_loading = nullptr;
  if (rc != 0) {
    error = "dino8_plugin_init returned " + std::to_string(rc);
    p.error = error;
    DoClose(handle);
    plugins_.push_back(p);
    return false;
  }
  p.loaded_ok = true;
  plugins_.push_back(std::move(p));
  return true;
}

bool Manager::Unload(const std::string& path) {
  for (size_t i = 0; i < plugins_.size(); ++i) {
    if (plugins_[i].path == path) {
      using ShutdownFn = void (*)();
      if (plugins_[i].handle) {
        if (auto fn = reinterpret_cast<ShutdownFn>(DoSymbol(plugins_[i].handle, "dino8_plugin_shutdown"))) fn();
        DoClose(plugins_[i].handle);
      }
      plugins_.erase(plugins_.begin() + static_cast<long>(i));
      return true;
    }
  }
  return false;
}

std::vector<PackageInfo> Manager::ScanPackages(const std::string& search_folder) const {
  std::vector<PackageInfo> out;
  std::error_code ec;
  if (!fs::exists(search_folder, ec) || !fs::is_directory(search_folder, ec)) return out;
  for (const auto& entry : fs::directory_iterator(search_folder, ec)) {
    if (!entry.is_directory()) continue;
    const fs::path manifest = entry.path() / "manifest.json";
    if (!fs::exists(manifest, ec)) continue;
    std::ifstream in(manifest);
    std::ostringstream ss;
    ss << in.rdbuf();
    json::Value root;
    std::string err;
    PackageInfo pkg;
    pkg.folder = entry.path().string();
    if (json::Parse(ss.str(), root, err) && root.IsObject()) {
      pkg.name = root["name"].AsString(entry.path().filename().string());
      pkg.version = root["version"].AsString("1.0.0");
      pkg.author = root["author"].AsString();
      pkg.description = root["description"].AsString();
    } else {
      pkg.name = entry.path().filename().string();
    }
    out.push_back(pkg);
  }
  return out;
}

bool Manager::InstallPackage(Application& app, const PackageInfo& pkg, std::string& error) {
  const std::string dest_dir = app::ConfigDirectory() + "/plugins";
  std::error_code ec;
  fs::create_directories(dest_dir, ec);
  int copied = 0;
  for (const auto& entry : fs::directory_iterator(pkg.folder, ec)) {
    if (!entry.is_regular_file()) continue;
    const std::string ext = entry.path().extension().string();
    if (ext == kLibExt || ext == ".lua" || ext == ".dflow") {
      fs::copy_file(entry.path(), fs::path(dest_dir) / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
      if (!ec) ++copied;
    }
  }
  if (copied == 0) { error = "package folder has no plug-in library, .lua script, or .dflow graph to install"; return false; }
  LoadFolder(app, dest_dir);
  return true;
}

int Manager::MigrateFrom(Application& app, const std::string& old_config_dir, std::string& error) {
  const std::string src = old_config_dir + "/plugins";
  std::error_code ec;
  if (!fs::exists(src, ec)) { error = "no plugins folder found at " + src; return 0; }
  const std::string dest = app::ConfigDirectory() + "/plugins";
  fs::create_directories(dest, ec);
  int migrated = 0;
  for (const auto& entry : fs::directory_iterator(src, ec)) {
    if (!entry.is_regular_file()) continue;
    fs::copy_file(entry.path(), fs::path(dest) / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
    if (!ec) ++migrated;
  }
  if (migrated > 0) LoadFolder(app, dest);
  return migrated;
}

}  // namespace dino8::plugins
