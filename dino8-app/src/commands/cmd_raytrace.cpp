// The Raytraced render path: Quality=Raytraced on Render / RenderPreview /
// RenderBlowup / BatchRenderNamedViews, the RayTracedViewport display mode
// (see cmd_view.cpp / Viewport::Render), and the built-in material library
// (MaterialLibrary command, RenderAssignMaterialToObjects Preset=). Wraps
// and re-registers a few commands cmd_render.cpp already owns - Register()
// keeps the last registration for a name, and this file is added to
// CMakeLists.txt and RegisterCommands() right after RegisterRenderCommands
// so these wrappers take over, delegating to the existing rasteriser
// (Application::RenderView) whenever Quality= isn't "Raytraced".
#include "commands/cmd_common.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <sstream>

#include "render/MaterialLibrary.h"
#include "render/PathTracer.h"

namespace dino8::app {

namespace {

std::string Lower(const std::string& s) {
  std::string t;
  for (char c : s) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return t;
}

// Same small "Key=Value" token parser cmd_render.cpp uses, duplicated here
// (it's ten lines and anonymous-namespace private there) so this file has
// no dependency on cmd_render.cpp's internals.
struct Args {
  std::map<std::string, std::string> kv;
  std::vector<std::string> rest;
  bool Has(const char* k) const { return kv.count(Lower(k)) > 0; }
  std::string Get(const char* k, const std::string& def = "") const { auto it = kv.find(Lower(k)); return it == kv.end() ? def : it->second; }
  double Num(const char* k, double def) const { auto it = kv.find(Lower(k)); if (it == kv.end()) return def; double v = def; std::sscanf(it->second.c_str(), "%lf", &v); return v; }
  bool Yes(const char* k, bool def) const { auto it = kv.find(Lower(k)); if (it == kv.end()) return def; const std::string v = Lower(it->second); return v == "yes" || v == "on" || v == "1" || v == "true"; }
};

Args TakeArgs(CommandContext& ctx) {
  Args a;
  while (auto t = ctx.Engine().TakePendingInput()) {
    const size_t eq = t->find('=');
    if (eq != std::string::npos) a.kv[Lower(t->substr(0, eq))] = t->substr(eq + 1);
    else a.rest.push_back(*t);
  }
  return a;
}

// "Key=Value" tokens anywhere in `pending` that name a quality option are
// consumed (matching cmd_render.cpp's ConsumeOptionTokens pattern) so an
// interactive command (RenderBlowup) can read Quality=/Samples=/Bounces=/
// Denoise= before it starts prompting for points.
Args TakeQualityTokensFromPending(CommandEngine& engine) {
  Args a;
  std::deque<std::string>& pending = engine.PendingInputs();
  static const char* kNames[] = {"quality", "samples", "bounces", "denoise"};
  for (auto it = pending.begin(); it != pending.end();) {
    const size_t eq = it->find('=');
    bool used = false;
    if (eq != std::string::npos) {
      const std::string key = Lower(it->substr(0, eq));
      for (const char* n : kNames) {
        if (key == n) { a.kv[key] = it->substr(eq + 1); used = true; break; }
      }
    }
    it = used ? pending.erase(it) : it + 1;
  }
  return a;
}

struct QualityArgs {
  bool raytraced = false;
  int samples = 64;
  int bounces = 8;
  bool denoise = true;
  double legacy_supersample = 0;  // Quality=<n>: the rasteriser's existing supersample factor
};

QualityArgs ParseQualityArgs(const Args& a) {
  QualityArgs q;
  if (a.Has("Quality")) {
    const std::string v = Lower(a.Get("Quality"));
    if (v == "raytraced" || v == "raytrace") q.raytraced = true;
    else std::sscanf(v.c_str(), "%lf", &q.legacy_supersample);
  }
  if (a.Has("Samples")) q.samples = std::max(1, static_cast<int>(a.Num("Samples", 64)));
  if (a.Has("Bounces")) q.bounces = std::max(1, static_cast<int>(a.Num("Bounces", 8)));
  q.denoise = a.Yes("Denoise", true);
  return q;
}

// Renders `vp`'s view (rasteriser or path tracer, per `q`) into
// Application::LastRender(). `seconds_out` is always filled on success.
bool RenderFullFrame(CommandContext& ctx, Viewport* vp, int w, int h, const QualityArgs& q, bool arctic,
                     std::string& error, double& seconds_out) {
  Application& app = ctx.App();
  if (!vp) vp = ctx.ActiveViewport();
  if (!vp) { error = "No active viewport"; return false; }
  if (!q.raytraced) {
    if (!app.RenderView(vp, w, h, static_cast<int>(q.legacy_supersample), arctic, error)) return false;
    seconds_out = app.LastRender().seconds;
    return true;
  }
  if (w <= 0) w = ctx.Doc().Render().render_width;
  if (h <= 0) h = ctx.Doc().Render().render_height;
  w = std::clamp(w, 16, 4096);
  h = std::clamp(h, 16, 4096);
  PathTracer tracer;
  const auto t0 = std::chrono::steady_clock::now();
  tracer.Prepare(ctx.Doc(), vp->GetCamera().State(), static_cast<double>(w) / h,
                 std::min(app.curve_display_tolerance, 0.01), std::min(app.surface_display_tolerance, 0.02));
  PathTraceSettings s;
  s.width = w; s.height = h; s.samples = std::max(q.samples, 1); s.bounces = std::max(q.bounces, 1);
  s.denoise = q.denoise; s.arctic = arctic;
  std::vector<unsigned char> rgb = tracer.Render(s);
  seconds_out = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  RenderImage& img = app.LastRender();
  if (img.texture) app.Renderer().DeleteTexture(img.texture);
  img = RenderImage{};
  img.width = w; img.height = h; img.rgb = std::move(rgb); img.view_name = vp->Name(); img.seconds = seconds_out;
  img.texture = app.Renderer().CreateTexture(w, h, img.rgb.data(), 3);
  app.Panels().render_window = true;
  return true;
}

void PrintRenderResult(CommandContext& ctx, const char* label, double seconds, const QualityArgs& q) {
  const RenderImage& img = ctx.App().LastRender();
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.2f", seconds);
  std::string msg = std::string(label) + ": rendered " + img.view_name + " at " + std::to_string(img.width) + " x " +
                    std::to_string(img.height) + " in " + buf + " s (Render Window)";
  if (q.raytraced) {
    msg += " [Raytraced Samples=" + std::to_string(std::max(q.samples, 1)) + " Bounces=" + std::to_string(std::max(q.bounces, 1)) +
           " Denoise=" + (q.denoise ? "Yes" : "No") + "]";
  }
  ctx.Print(msg);
}

// RenderBlowup: pick a window in the viewport, render the full view (at
// whichever Quality= the line carried), crop. Re-registers cmd_render.cpp's
// RenderBlowup so Quality= reaches it too.
class RaytraceAwareRenderBlowupCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    quality_ = ParseQualityArgs(TakeQualityTokensFromPending(ctx.Engine()));
    WantPoint("First corner of the region to render");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    if (pts_.size() == 1) { WantPoint("Other corner"); return; }
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    double x0, y0, x1, y1;
    if (!vp->WorldToPixel(pts_[0], x0, y0) || !vp->WorldToPixel(pts_[1], x1, y1)) { ctx.Warn("Region is off screen"); Finish(); return; }
    const int w = std::max(vp->Width(), 1), h = std::max(vp->Height(), 1);
    std::string err; double seconds = 0;
    if (!RenderFullFrame(ctx, vp, w, h, quality_, false, err, seconds)) { ctx.Warn(err); Finish(); return; }
    RenderImage& img = ctx.App().LastRender();
    const int ax = std::clamp(static_cast<int>(std::min(x0, x1)), 0, w - 1), bx = std::clamp(static_cast<int>(std::max(x0, x1)), 1, w);
    const int ay = std::clamp(static_cast<int>(std::min(y0, y1)), 0, h - 1), by = std::clamp(static_cast<int>(std::max(y0, y1)), 1, h);
    const int cw = std::max(bx - ax, 1), ch = std::max(by - ay, 1);
    std::vector<unsigned char> crop(static_cast<size_t>(cw) * ch * 3);
    for (int y = 0; y < ch; ++y) std::memcpy(&crop[static_cast<size_t>(y) * cw * 3], &img.rgb[(static_cast<size_t>(ay + y) * w + ax) * 3], static_cast<size_t>(cw) * 3);
    if (img.texture) ctx.App().Renderer().DeleteTexture(img.texture);
    img.width = cw; img.height = ch; img.rgb = std::move(crop);
    img.texture = ctx.App().Renderer().CreateTexture(cw, ch, img.rgb.data(), 3);
    std::string msg = "RenderBlowup: " + std::to_string(cw) + " x " + std::to_string(ch) + " region rendered (Partial: cropped from a full-view render)";
    if (quality_.raytraced) msg += " [Raytraced]";
    ctx.Print(msg);
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.empty()) return;
    ctx.ClearPreview();
    ON_Plane pl = ActivePlane(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(pts_[0], &u0, &v0); pl.ClosestPointTo(h, &u1, &v1);
    ctx.AddPreviewPolyline({pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)}, true);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  QualityArgs quality_;
  std::vector<Point3d> pts_;
};

// Case-insensitive material-option keys, extended with Preset=<name> (a
// built-in MaterialLibrary preset overwrites every appearance field at
// once; other Key=Value edits still apply afterwards on top of it).
class LibraryAwareAssignMaterialCommand : public Command {
 public:
  explicit LibraryAwareAssignMaterialCommand(bool to_layers) : to_layers_(to_layers) {}
  void Begin(CommandContext& ctx) override {
    std::deque<std::string>& pending = ctx.Engine().PendingInputs();
    for (auto it = pending.begin(); it != pending.end();) {
      const size_t eq = it->find('=');
      if (eq != std::string::npos) { edits_.emplace_back(Lower(it->substr(0, eq)), it->substr(eq + 1)); it = pending.erase(it); }
      else ++it;
    }
    for (const auto& [k, v] : edits_) if (k == "preset") preset_ = v;
    if (auto t = ctx.Engine().TakePendingInput()) { name_ = *t; WantObjects("Select objects"); return; }
    if (!preset_.empty()) { name_ = preset_; WantObjects("Select objects"); return; }
    WantText("Material name (Enter to list). Options: Preset= Color=r,g,b Gloss= Transparency= Reflectivity= Texture= Mapping= Scale=");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (name_.empty()) { name_ = t; WantObjects("Select objects"); return; }
    (void)ctx;
  }
  void OnEnter(CommandContext& ctx) override {
    if (name_.empty()) {
      if (ctx.Doc().Materials().empty()) ctx.Print("No materials in the document (use Materials to create one, or MaterialLibrary to see the presets)");
      for (const Material& m : ctx.Doc().Materials()) ctx.Print("  " + m.name);
      Finish();
    }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    Document& doc = ctx.Doc();
    doc.BeginChange("Assign material");
    Material* m = doc.FindMaterial(name_);
    const MaterialPreset* preset = preset_.empty() ? nullptr : FindMaterialPreset(preset_);
    if (!preset_.empty() && !preset) ctx.Warn("RenderAssignMaterialToObjects: unknown Preset=" + preset_ + " (see MaterialLibrary for the list)");
    if (!m) {
      Material nm;
      nm.name = name_;
      if (preset) nm = preset->material;
      else if (const SceneObject* o = doc.Find(ids.front())) nm.diffuse = doc.MaterialFor(*o).diffuse;
      nm.name = name_;
      doc.AddMaterial(nm);
      m = doc.FindMaterial(name_);
      ctx.Print(std::string("Created material ") + name_ + (preset ? " from preset " + preset_ : ""));
    } else if (preset) {
      const std::string keep_name = m->name;
      *m = preset->material;
      m->name = keep_name;
      ctx.Print("Material " + name_ + ": preset " + preset_ + " applied");
    }
    if (m && !edits_.empty()) {
      std::string applied;
      for (const auto& [k, v] : edits_) {
        if (k == "preset") continue;
        double d = 0;
        int r, g, b;
        if (k == "color" && std::sscanf(v.c_str(), "%d,%d,%d", &r, &g, &b) == 3) m->diffuse = Color::FromBytes(r, g, b);
        else if (k == "gloss" && std::sscanf(v.c_str(), "%lf", &d) == 1) m->gloss = static_cast<float>(std::clamp(d, 0.0, 1.0));
        else if (k == "transparency" && std::sscanf(v.c_str(), "%lf", &d) == 1) m->transparency = static_cast<float>(std::clamp(d, 0.0, 1.0));
        else if (k == "reflectivity" && std::sscanf(v.c_str(), "%lf", &d) == 1) m->reflectivity = static_cast<float>(std::clamp(d, 0.0, 1.0));
        else if (k == "texture") { m->texture_path = v; ctx.App().Renderer().RefreshTextures(); }
        else if (k == "mapping") ParseTextureMapping(v, m->mapping);
        else if (k == "scale" && std::sscanf(v.c_str(), "%lf", &d) == 1 && d > 0) m->mapping_scale = static_cast<float>(d);
        else { ctx.Warn("Unknown material option " + k + "=" + v); continue; }
        applied += " " + k + "=" + v;
      }
      if (!applied.empty()) ctx.Print("Material " + name_ + ":" + applied);
    }
    int n = 0;
    for (ObjectId id : ids) {
      SceneObject* o = doc.Find(id);
      if (!o) continue;
      if (to_layers_) {
        if (o->layer_index >= 0 && o->layer_index < static_cast<int>(doc.Layers().size())) { doc.Layers()[static_cast<size_t>(o->layer_index)].material = name_; ++n; }
        o->material_name.clear();
      } else {
        o->material_name = name_;
        ++n;
      }
      o->InvalidateDisplay();
    }
    doc.Touch();
    ctx.Print("Material " + name_ + (to_layers_ ? " assigned to the layers of " : " assigned to ") + std::to_string(n) + " object(s)");
    Finish();
  }
  bool to_layers_;
  std::string name_, preset_;
  std::vector<std::pair<std::string, std::string>> edits_;
};

}  // namespace

void RegisterRaytraceCommands(CommandEngine& e) {
  Reg(e, "Render", Immediate([](CommandContext& ctx) {
        Args a = TakeArgs(ctx);
        int w = static_cast<int>(a.Num("Width", 0)), h = static_cast<int>(a.Num("Height", 0));
        if (a.rest.size() >= 2) { w = std::atoi(a.rest[0].c_str()); h = std::atoi(a.rest[1].c_str()); }
        const QualityArgs q = ParseQualityArgs(a);
        std::string err; double seconds = 0;
        if (!RenderFullFrame(ctx, nullptr, w, h, q, false, err, seconds)) { ctx.Warn(std::string("Render: ") + err); return; }
        PrintRenderResult(ctx, "Render", seconds, q);
      }));
  auto preview = [](CommandContext& ctx, int fast_samples) {
    Args a = TakeArgs(ctx);
    QualityArgs q = ParseQualityArgs(a);
    if (q.raytraced && !a.Has("Samples")) q.samples = fast_samples;
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) return;
    const int w = std::max(vp->Width(), 16), h = std::max(vp->Height(), 16);
    std::string err; double seconds = 0;
    if (!RenderFullFrame(ctx, vp, w, h, q, false, err, seconds)) { ctx.Warn(std::string("RenderPreview: ") + err); return; }
    PrintRenderResult(ctx, "RenderPreview", seconds, q);
  };
  Reg(e, "RenderPreview", Immediate([preview](CommandContext& ctx) { preview(ctx, 8); }));
  Reg(e, "RenderPreviewInWindow", Immediate([preview](CommandContext& ctx) { preview(ctx, 8); }));
  Reg(e, "RenderPreviewWindow", Immediate([preview](CommandContext& ctx) { preview(ctx, 8); }));
  Reg(e, "RenderArctic", Immediate([](CommandContext& ctx) {
        Args a = TakeArgs(ctx);
        const QualityArgs q = ParseQualityArgs(a);
        std::string err; double seconds = 0;
        if (!RenderFullFrame(ctx, nullptr, 0, 0, q, true, err, seconds)) { ctx.Warn(std::string("RenderArctic: ") + err); return; }
        PrintRenderResult(ctx, "RenderArctic", seconds, q);
      }));
  Reg(e, "RenderBlowup", Make<RaytraceAwareRenderBlowupCommand>(), CommandStatus::Partial,
      "Crops a full-view render to the picked region; honours Quality=Raytraced.");
  Reg(e, "BatchRenderNamedViews", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        Args a = TakeArgs(ctx);
        const QualityArgs q = ParseQualityArgs(a);
        auto run = [&app, &ctx, vp, &q](const std::string& folder) {
          std::error_code ec;
          std::filesystem::create_directories(folder, ec);
          Document& doc = ctx.Doc();
          if (doc.NamedViews().empty()) { ctx.Warn("BatchRenderNamedViews: the document has no named views"); return; }
          const CameraState saved = vp->GetCamera().State();
          int n = 0;
          for (const NamedView& nv : doc.NamedViews()) {
            vp->GetCamera().SetState(nv.camera);
            std::string err; double seconds = 0;
            if (!RenderFullFrame(ctx, vp, 0, 0, q, false, err, seconds)) { ctx.Warn(nv.name + ": " + err); continue; }
            std::string file = nv.name;
            for (char& c : file) if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
            const std::string path = folder + "/" + file + ".bmp";
            if (app.SaveLastRender(path, err)) { ctx.Print("  " + nv.name + " -> " + path); ++n; } else ctx.Warn(err);
          }
          vp->GetCamera().SetState(saved);
          ctx.Print(std::string("BatchRenderNamedViews: ") + std::to_string(n) + " view(s) rendered to " + folder + (q.raytraced ? " [Raytraced]" : ""));
        };
        if (auto p = a.rest.empty() ? std::nullopt : std::optional<std::string>(a.rest[0])) { run(*p); return; }
        app.ShowFileDialog("Folder for the renderings (pick any file name in it)", {".bmp"}, true, [run](const std::string& path) { run(std::filesystem::path(path).parent_path().string()); });
      }));

  // ---- material library ---------------------------------------------------
  Reg(e, "RenderAssignMaterialToObjects", Make<LibraryAwareAssignMaterialCommand>(false));
  Reg(e, "RenderAssignMaterialToLayersOfObjects", Make<LibraryAwareAssignMaterialCommand>(true));
  Reg(e, "MaterialLibrary", Immediate([](CommandContext& ctx) {
        ctx.App().Panels().materials = true;
        ctx.Print("MaterialLibrary: " + std::to_string(MaterialPresets().size()) + " built-in preset(s) (see the Materials panel's Library section, or RenderAssignMaterialToObjects Preset=<name>):");
        for (const MaterialPreset& p : MaterialPresets()) ctx.Print("  " + p.name);
      }));
}

}  // namespace dino8::app
