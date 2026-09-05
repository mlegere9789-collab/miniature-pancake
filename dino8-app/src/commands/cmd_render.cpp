// Rendering: materials, texture mapping, lights, environment, ground plane
// and the built-in renderer (Render / RenderPreview / SaveRenderWindowAs...).
#include "commands/cmd_common.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <sstream>

#include "app/Settings.h"
#include "render/ImageIO.h"
#include "ui/Panels.h"

namespace dino8::app {

namespace {

std::string Lower(const std::string& s) {
  std::string t;
  for (char c : s) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return t;
}

// Tokens typed after an immediate command: "Key=Value" pairs and the rest.
struct Args {
  std::map<std::string, std::string> kv;  // lower-case key -> value
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

// Pulls "Key=Value" tokens out of the pending inputs (wherever they were
// typed on the line) and applies them through OnOption before the command
// starts asking for points, so "PointLight 0,0,10 Intensity=2" works.
void ConsumeOptionTokens(CommandContext& ctx, Command& cmd) {
  std::deque<std::string>& pending = ctx.Engine().PendingInputs();
  for (auto it = pending.begin(); it != pending.end();) {
    const size_t eq = it->find('=');
    bool used = false;
    if (eq != std::string::npos) {
      const std::string key = Lower(it->substr(0, eq)), value = it->substr(eq + 1);
      for (const OptionSpec& o : cmd.options) {
        if (Lower(o.name) == key) { cmd.OnOption(ctx, o.name, value); used = true; break; }
      }
    }
    it = used ? pending.erase(it) : it + 1;
  }
}

bool ParseColor(const std::string& text, Color& out) {
  int r, g, b;
  if (std::sscanf(text.c_str(), "%d,%d,%d", &r, &g, &b) == 3) { out = Color::FromBytes(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255)); return true; }
  static const std::map<std::string, Color> named = {
      {"white", Color::FromBytes(255, 255, 255)}, {"black", Color::FromBytes(0, 0, 0)}, {"red", Color::FromBytes(220, 40, 40)},
      {"green", Color::FromBytes(40, 180, 60)}, {"blue", Color::FromBytes(50, 90, 220)}, {"yellow", Color::FromBytes(240, 220, 60)},
      {"gray", Color::FromBytes(128, 128, 128)}, {"grey", Color::FromBytes(128, 128, 128)}, {"orange", Color::FromBytes(240, 150, 40)}};
  auto it = named.find(Lower(text));
  if (it == named.end()) return false;
  out = it->second;
  return true;
}

std::string ColorText(const Color& c) {
  return std::to_string(static_cast<int>(c.r * 255 + 0.5f)) + "," + std::to_string(static_cast<int>(c.g * 255 + 0.5f)) + "," + std::to_string(static_cast<int>(c.b * 255 + 0.5f));
}

std::string UniqueMaterialName(const Document& doc, const std::string& base) {
  if (!doc.FindMaterial(base)) return base;
  for (int i = 2; i < 10000; ++i) {
    const std::string n = base + " " + std::to_string(i);
    if (!doc.FindMaterial(n)) return n;
  }
  return base;
}

// The material an object should get edited: its own, else a new one made
// from its display colour and assigned to it.
Material& OwnMaterial(Document& doc, SceneObject& o) {
  if (!o.material_name.empty()) {
    if (Material* m = doc.FindMaterial(o.material_name)) return *m;
  }
  Material m = doc.MaterialFor(o);
  m.name = UniqueMaterialName(doc, o.name.empty() ? "Material " + std::to_string(o.id) : o.name);
  const std::string name = doc.AddMaterial(m);
  o.material_name = name;
  o.InvalidateDisplay();
  return *doc.FindMaterial(name);
}

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

class LightCommand : public Command {
 public:
  explicit LightCommand(LightType type) : type_(type) {}
  void Begin(CommandContext& ctx) override {
    options = {{"Intensity", FormatNumber(light_.intensity), {}, true, false},
               {"Color", ColorText(light_.color), {}, false, false},
               {"Name", "", {}, false, false},
               {"Enabled", "Yes", {"Yes", "No"}, false, true}};
    if (type_ == LightType::Spot) options.push_back({"Angle", FormatNumber(light_.spot_angle), {}, true, false});
    if (type_ == LightType::Rectangular) options.push_back({"Width", FormatNumber(light_.width), {}, true, false});
    ConsumeOptionTokens(ctx, *this);
    switch (type_) {
      case LightType::Point: WantPoint("Location of point light"); break;
      case LightType::Spot: WantPoint("Base of cone"); break;
      case LightType::Directional: WantPoint("Start of light direction"); break;
      case LightType::Rectangular: WantPoint("First corner of rectangle"); break;
      case LightType::Linear: WantPoint("Start of linear light"); break;
    }
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    double d = 0;
    if (n == "Intensity" && std::sscanf(v.c_str(), "%lf", &d) == 1) light_.intensity = static_cast<float>(std::max(0.0, d));
    else if (n == "Color") { Color c; if (ParseColor(v, c)) light_.color = c; else ctx.Warn("Color: use r,g,b (0-255) or a colour name"); }
    else if (n == "Name") light_.name = v;
    else if (n == "Enabled") light_.enabled = Lower(v) == "yes";
    else if (n == "Angle" && std::sscanf(v.c_str(), "%lf", &d) == 1) light_.spot_angle = static_cast<float>(std::clamp(d, 1.0, 89.0));
    else if (n == "Width" && std::sscanf(v.c_str(), "%lf", &d) == 1) light_.width = std::max(0.01, d);
    for (OptionSpec& o : options) {
      if (o.name == "Intensity") o.value = FormatNumber(light_.intensity);
      if (o.name == "Color") o.value = ColorText(light_.color);
      if (o.name == "Name") o.value = light_.name;
      if (o.name == "Enabled") o.value = light_.enabled ? "Yes" : "No";
      if (o.name == "Angle") o.value = FormatNumber(light_.spot_angle);
      if (o.name == "Width") o.value = FormatNumber(light_.width);
    }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    switch (type_) {
      case LightType::Point:
        light_.position = p;
        Create(ctx);
        return;
      case LightType::Spot:
        if (pts_.size() == 1) { WantPoint("Radius of cone base <" + FormatNumber(radius_) + ">"); return; }
        if (pts_.size() == 2 && !have_radius_) { radius_ = (p - pts_[0]).Length(); have_radius_ = true; WantPoint("End of cone (light location)"); return; }
        {
          const Point3d base = pts_[0];
          light_.position = p;
          Vector3d d = base - p;
          const double len = d.Length();
          if (!d.Unitize()) { ctx.Warn("The cone end must differ from its base"); Finish(); return; }
          light_.direction = d;
          light_.length = len;
          light_.spot_angle = static_cast<float>(std::clamp(std::atan2(std::max(radius_, 1e-9), std::max(len, 1e-9)) * 180.0 / ON_PI, 1.0, 89.0));
          Create(ctx);
        }
        return;
      case LightType::Directional:
        if (pts_.size() == 1) { WantPoint("End of light direction"); return; }
        {
          Vector3d d = pts_[1] - pts_[0];
          if (!d.Unitize()) { ctx.Warn("Start and end coincide"); Finish(); return; }
          light_.position = pts_[0];
          light_.direction = d;
          Create(ctx);
        }
        return;
      case LightType::Rectangular:
        if (pts_.size() == 1) { WantPoint("Other corner of rectangle"); return; }
        {
          ON_Plane pl = ActivePlane(ctx);
          double u0, v0, u1, v1;
          pl.ClosestPointTo(pts_[0], &u0, &v0);
          pl.ClosestPointTo(pts_[1], &u1, &v1);
          light_.position = pl.PointAt((u0 + u1) / 2, (v0 + v1) / 2);
          light_.x_axis = pl.xaxis;
          light_.length = std::max(std::fabs(u1 - u0), 0.01);
          light_.width = std::max(std::fabs(v1 - v0), 0.01);
          light_.direction = -pl.zaxis;  // shines down through the construction plane
          Create(ctx);
        }
        return;
      case LightType::Linear:
        if (pts_.size() == 1) { WantPoint("End of linear light"); return; }
        {
          Vector3d d = pts_[1] - pts_[0];
          light_.length = d.Length();
          if (!d.Unitize()) { ctx.Warn("Start and end coincide"); Finish(); return; }
          light_.position = pts_[0];
          light_.x_axis = d;
          light_.direction = -ActiveNormal(ctx);
          Create(ctx);
        }
        return;
    }
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (type_ == LightType::Spot && pts_.size() == 1 && !have_radius_) {
      radius_ = std::max(v, 1e-6);
      have_radius_ = true;
      WantPoint("End of cone (light location)");
      return;
    }
    (void)ctx;
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    double v = 0;
    if (std::sscanf(t.c_str(), "%lf", &v) == 1) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override {
    if (type_ == LightType::Spot && pts_.size() == 1 && !have_radius_) { have_radius_ = true; WantPoint("End of cone (light location)"); return; }
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.empty()) return;
    ctx.ClearPreview();
    if (type_ == LightType::Rectangular) {
      ON_Plane pl = ActivePlane(ctx);
      double u0, v0, u1, v1;
      pl.ClosestPointTo(pts_[0], &u0, &v0);
      pl.ClosestPointTo(h, &u1, &v1);
      ctx.AddPreviewPolyline({pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)}, true);
    } else {
      ctx.AddPreviewLine(pts_[0], h);
    }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  void Create(CommandContext& ctx) {
    ctx.ClearPreview();
    light_.type = type_;
    ctx.Doc().BeginChange(std::string(LightTypeName(type_)) + " light");
    const int id = ctx.Doc().AddLight(light_);
    const Light* L = ctx.Doc().FindLight(id);
    std::string msg = std::string(LightTypeName(type_)) + " light added";
    if (L) {
      msg = L->name + ": " + LightTypeName(type_) + " light";
      if (type_ == LightType::Directional) msg += " direction " + FormatPoint(Point3d(L->direction.x, L->direction.y, L->direction.z));
      else msg += " at " + FormatPoint(L->position);
      if (type_ == LightType::Spot) msg += ", cone angle " + FormatNumber(L->spot_angle) + " deg";
      msg += ", Intensity=" + FormatNumber(L->intensity) + (L->enabled ? "" : " (disabled)");
    }
    ctx.Print(msg);
    Finish();
  }
  LightType type_;
  Light light_;
  std::vector<Point3d> pts_;
  double radius_ = 5.0;
  bool have_radius_ = false;
};

std::vector<Light*> SelectedLights(Document& doc, bool fallback_to_all) {
  std::vector<Light*> out;
  for (Light& l : doc.Lights()) if (l.selected) out.push_back(&l);
  if (out.empty() && fallback_to_all) for (Light& l : doc.Lights()) out.push_back(&l);
  return out;
}

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

// "RenderAssignMaterialToObjects <name>" then a selection.
class AssignMaterialCommand : public Command {
 public:
  explicit AssignMaterialCommand(bool to_layers) : to_layers_(to_layers) {}
  void Begin(CommandContext& ctx) override {
    // "Key=Value" tokens anywhere on the line edit the material.
    std::deque<std::string>& pending = ctx.Engine().PendingInputs();
    for (auto it = pending.begin(); it != pending.end();) {
      const size_t eq = it->find('=');
      if (eq != std::string::npos) { edits_.emplace_back(Lower(it->substr(0, eq)), it->substr(eq + 1)); it = pending.erase(it); }
      else ++it;
    }
    if (auto t = ctx.Engine().TakePendingInput()) { name_ = *t; WantObjects("Select objects"); return; }
    WantText("Material name (Enter to list). Options: Color=r,g,b Gloss= Transparency= Reflectivity= Texture= Mapping= Scale=");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (name_.empty()) { name_ = t; WantObjects("Select objects"); return; }
    (void)ctx;
  }
  void OnEnter(CommandContext& ctx) override {
    if (name_.empty()) {
      if (ctx.Doc().Materials().empty()) ctx.Print("No materials in the document (use Materials to create one)");
      for (const Material& m : ctx.Doc().Materials()) ctx.Print("  " + m.name + "  colour " + ColorText(m.diffuse));
      Finish();
    }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    Document& doc = ctx.Doc();
    Material* m = doc.FindMaterial(name_);
    doc.BeginChange("Assign material");
    if (!m) {
      // Create it on the fly with the first object's colour.
      Material nm;
      nm.name = name_;
      if (const SceneObject* o = doc.Find(ids.front())) nm.diffuse = doc.MaterialFor(*o).diffuse;
      doc.AddMaterial(nm);
      m = doc.FindMaterial(name_);
      ctx.Print("Created material " + name_);
    }
    if (m && !edits_.empty()) {
      std::string applied;
      for (const auto& [k, v] : edits_) {
        double d = 0;
        Color c;
        if (k == "color" && ParseColor(v, c)) m->diffuse = c;
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
  std::string name_;
  std::vector<std::pair<std::string, std::string>> edits_;
};

// ApplyXMapping [Scale=n]: selection -> per-object mapping override.
class MappingCommand : public Command {
 public:
  explicit MappingCommand(TextureMapping mapping) : mapping_(mapping) {}
  void Begin(CommandContext& ctx) override {
    options = {{"Scale", FormatNumber(scale_), {}, true, false}};
    ConsumeOptionTokens(ctx, *this);
    WantObjects("Select objects for " + std::string(TextureMappingName(mapping_)) + " mapping");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    double d;
    if (n == "Scale" && std::sscanf(v.c_str(), "%lf", &d) == 1 && d > 0) { scale_ = static_cast<float>(d); options[0].value = FormatNumber(d); }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ctx.Doc().BeginChange(std::string("Apply") + TextureMappingName(mapping_) + "Mapping");
    int n = 0;
    for (ObjectId id : ids) {
      if (SceneObject* o = ctx.Doc().Find(id)) { o->mapping = mapping_; o->mapping_scale = scale_; o->InvalidateDisplay(); ++n; }
    }
    ctx.Print(std::string(TextureMappingName(mapping_)) + " mapping applied to " + std::to_string(n) + " object(s), Scale=" + FormatNumber(scale_) +
              (mapping_ == TextureMapping::Custom ? " (Custom mapping uses a planar projection; the widget is planned)" : ""));
    Finish();
  }
  TextureMapping mapping_;
  float scale_ = 1.f;
};

class MatchMappingCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select the source object", 1); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!source_) {
      source_ = ids.front();
      ctx.Doc().SelectNone();
      accept_preselection = false;
      WantObjects("Select objects to change");
      return;
    }
    const SceneObject* src = ctx.Doc().Find(*source_);
    if (!src) { Finish(); return; }
    const TextureMapping mapping = src->mapping;
    const float scale = src->mapping_scale;
    ctx.Doc().BeginChange("MatchMapping");
    int n = 0;
    for (ObjectId id : ids) {
      if (id == *source_) continue;
      if (SceneObject* o = ctx.Doc().Find(id)) { o->mapping = mapping; o->mapping_scale = scale; o->InvalidateDisplay(); ++n; }
    }
    ctx.Print("MatchMapping: " + std::string(TextureMappingName(mapping)) + " mapping copied to " + std::to_string(n) + " object(s)");
    Finish();
  }
  std::optional<ObjectId> source_;
};

// Picture <image> then two corners: a plane with the image as texture.
class PictureCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { SetPath(ctx, *t); return; }
    if (ctx.ScriptMode()) { WantText("Image file"); return; }
    Application& app = ctx.App();
    app.ShowFileDialog("Picture image", {".bmp", ".ppm", ".pgm", ".png"}, false, [&app](const std::string& path) { app.Engine().Execute("Picture " + path); });
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { if (path_.empty()) SetPath(ctx, t); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) { WantPoint("Other corner (Enter to keep the image aspect ratio)"); return; }
    Build(ctx, pts_[1]);
  }
  void OnEnter(CommandContext& ctx) override {
    if (pts_.size() == 1) { ON_Plane pl = ActivePlane(ctx); Build(ctx, pts_[0] + pl.xaxis * 10.0 + pl.yaxis * (10.0 * image_.height / std::max(image_.width, 1))); return; }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.size() != 1) return;
    ctx.ClearPreview();
    ON_Plane pl = ActivePlane(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(pts_[0], &u0, &v0); pl.ClosestPointTo(h, &u1, &v1);
    ctx.AddPreviewPolyline({pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)}, true);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  void SetPath(CommandContext& ctx, const std::string& path) {
    std::string err;
    if (!LoadImageFile(path, image_, err)) { ctx.Warn("Picture: " + err); Finish(); return; }
    path_ = path;
    ctx.Print("Picture: " + std::to_string(image_.width) + " x " + std::to_string(image_.height) + " image");
    WantPoint("First corner of picture");
  }
  void Build(CommandContext& ctx, Point3d other) {
    ctx.ClearPreview();
    ON_Plane pl = ActivePlane(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(pts_[0], &u0, &v0);
    pl.ClosestPointTo(other, &u1, &v1);
    if (std::fabs(u1 - u0) < 1e-9 || std::fabs(v1 - v0) < 1e-9) { ctx.Warn("Picture: corners coincide"); Finish(); return; }
    // Keep u increasing to the right and v up so the image is not mirrored.
    const double ua = std::min(u0, u1), ub = std::max(u0, u1), va = std::min(v0, v1), vb = std::max(v0, v1);
    std::vector<Point3d> grid = {pl.PointAt(ua, va), pl.PointAt(ub, va), pl.PointAt(ua, vb), pl.PointAt(ub, vb)};
    Document& doc = ctx.Doc();
    doc.BeginChange("Picture");
    Material m;
    m.name = UniqueMaterialName(doc, std::filesystem::path(path_).stem().string());
    m.diffuse = Color::FromBytes(255, 255, 255);
    m.gloss = 0.05f;
    m.texture_path = path_;
    m.mapping = TextureMapping::Surface;
    const std::string name = doc.AddMaterial(m);
    SceneObject o = SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1));
    o.name = std::filesystem::path(path_).stem().string();
    o.material_name = name;
    doc.Add(std::move(o));
    ctx.Print("Picture: plane created with material " + name + " (" + path_ + ")");
    Finish();
  }
  std::string path_;
  Image image_;
  std::vector<Point3d> pts_;
};

// RenderBlowup: pick a window in the viewport, render, crop.
class RenderBlowupCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("First corner of the region to render"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    if (pts_.size() == 1) { WantPoint("Other corner"); return; }
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    double x0, y0, x1, y1;
    if (!vp->WorldToPixel(pts_[0], x0, y0) || !vp->WorldToPixel(pts_[1], x1, y1)) { ctx.Warn("Region is off screen"); Finish(); return; }
    std::string err;
    Application& app = ctx.App();
    const int w = std::max(vp->Width(), 1), h = std::max(vp->Height(), 1);
    if (!app.RenderView(vp, w, h, 0, false, err)) { ctx.Warn(err); Finish(); return; }
    RenderImage& img = app.LastRender();
    const int ax = std::clamp(static_cast<int>(std::min(x0, x1)), 0, w - 1), bx = std::clamp(static_cast<int>(std::max(x0, x1)), 1, w);
    const int ay = std::clamp(static_cast<int>(std::min(y0, y1)), 0, h - 1), by = std::clamp(static_cast<int>(std::max(y0, y1)), 1, h);
    const int cw = std::max(bx - ax, 1), ch = std::max(by - ay, 1);
    std::vector<unsigned char> crop(static_cast<size_t>(cw) * ch * 3);
    for (int y = 0; y < ch; ++y) std::memcpy(&crop[static_cast<size_t>(y) * cw * 3], &img.rgb[(static_cast<size_t>(ay + y) * w + ax) * 3], static_cast<size_t>(cw) * 3);
    if (img.texture) app.Renderer().DeleteTexture(img.texture);
    img.width = cw; img.height = ch; img.rgb = std::move(crop);
    img.texture = app.Renderer().CreateTexture(cw, ch, img.rgb.data(), 3);
    ctx.Print("RenderBlowup: " + std::to_string(cw) + " x " + std::to_string(ch) + " region rendered (Partial: cropped from a full-view render)");
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
  std::vector<Point3d> pts_;
};

// ---------------------------------------------------------------------------
// Helpers for immediate render commands
// ---------------------------------------------------------------------------

std::string WithImageExt(std::string p) {
  const std::string ext = Lower(std::filesystem::path(p).extension().string());
  if (ext != ".bmp" && ext != ".ppm") p += ".bmp";
  return p;
}

void DoRender(CommandContext& ctx, int w, int h, int quality, bool arctic, const char* label) {
  std::string err;
  Application& app = ctx.App();
  if (!app.RenderView(nullptr, w, h, quality, arctic, err)) { ctx.Warn(std::string(label) + ": " + err); return; }
  const RenderImage& img = app.LastRender();
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.2f", img.seconds);
  ctx.Print(std::string(label) + ": rendered " + img.view_name + " at " + std::to_string(img.width) + " x " + std::to_string(img.height) + " in " + buf + " s (Render Window)");
}

// Textures referenced by the document with their existence state.
std::vector<std::pair<std::string, bool>> ImageFiles(const Document& doc) {
  std::vector<std::pair<std::string, bool>> files;
  auto add = [&](const std::string& p) {
    if (p.empty()) return;
    for (const auto& f : files) if (f.first == p) return;
    std::error_code ec;
    files.emplace_back(p, std::filesystem::exists(p, ec));
  };
  for (const Material& m : doc.Materials()) add(m.texture_path);
  add(doc.Render().environment_image);
  return files;
}

kernel::Mesh MeshFromTriangles(const std::vector<float>& tri) {
  kernel::Mesh k;
  ON_Mesh& m = k.raw();
  const int n = static_cast<int>(tri.size() / 18);
  m.m_V.Reserve(n * 3);
  m.m_F.Reserve(n);
  for (int i = 0; i < n; ++i) {
    ON_MeshFace f;
    for (int c = 0; c < 3; ++c) {
      const float* v = &tri[static_cast<size_t>(i) * 18 + static_cast<size_t>(c) * 6];
      f.vi[c] = m.m_V.Count();
      m.m_V.Append(ON_3fPoint(v[0], v[1], v[2]));
    }
    f.vi[3] = f.vi[2];
    m.m_F.Append(f);
  }
  m.CombineIdenticalVertices(true, true);
  m.ComputeFaceNormals();
  m.ComputeVertexNormals();
  return k;
}

}  // namespace

void RegisterRenderCommands(CommandEngine& e) {
  // ---- lights -----------------------------------------------------------
  Reg(e, "PointLight", Make<LightCommand>(LightType::Point));
  Reg(e, "Spotlight", Make<LightCommand>(LightType::Spot));
  Reg(e, "DirectionalLight", Make<LightCommand>(LightType::Directional));
  Reg(e, "RectangularLight", Make<LightCommand>(LightType::Rectangular));
  Reg(e, "LinearLight", Make<LightCommand>(LightType::Linear));
  Reg(e, "Lights", Immediate([](CommandContext& ctx) { ctx.App().Panels().lights = true; }));
  Reg(e, "SelLight", Immediate([](CommandContext& ctx) {
        int n = 0;
        for (Light& l : ctx.Doc().Lights()) { l.selected = true; ++n; }
        ctx.Print(std::to_string(n) + " light(s) selected");
      }));
  Reg(e, "SetSpotlightToView", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        int n = 0;
        const CameraState& c = vp->GetCamera().State();
        for (Light* l : SelectedLights(ctx.Doc(), true)) {
          if (l->type != LightType::Spot) continue;
          if (n == 0) ctx.Doc().BeginChange("SetSpotlightToView");
          l->position = c.eye;
          Vector3d d = c.target - c.eye;
          l->length = d.Length();
          if (d.Unitize()) l->direction = d;
          ++n;
        }
        if (n) ctx.Print("SetSpotlightToView: " + std::to_string(n) + " spotlight(s) moved to the " + vp->Name() + " camera"); else ctx.Warn("No spotlight selected (SelLight selects lights)");
      }));
  Reg(e, "SetViewToSpotlight", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        for (Light* l : SelectedLights(ctx.Doc(), true)) {
          if (l->type != LightType::Spot) continue;
          CameraState& c = vp->GetCamera().State();
          c.eye = l->position;
          c.target = l->position + l->direction * (l->length > 0 ? l->length : 10.0);
          c.perspective = true;
          ctx.Print("SetViewToSpotlight: " + vp->Name() + " now looks through " + l->name);
          return;
        }
        ctx.Warn("No spotlight selected (SelLight selects lights)");
      }));
  Reg(e, "EditLightByHighlight", Immediate([](CommandContext& ctx) { ctx.App().Panels().lights = true; ctx.Print("EditLightByHighlight: edit the light's position, direction and cone in the Lights panel."); }), CommandStatus::Partial, "Uses the Lights panel; interactive highlight editing is planned.");
  Reg(e, "EditLightByLooking", Immediate([](CommandContext& ctx) { ctx.App().Panels().lights = true; ctx.Print("EditLightByLooking: use SetSpotlightToView after aiming the view, or edit in the Lights panel."); }), CommandStatus::Partial, "Uses SetSpotlightToView / the Lights panel.");
  Reg(e, "Sun", Immediate([](CommandContext& ctx) {
        RenderSettings& r = ctx.Doc().Render();
        Args a = TakeArgs(ctx);
        const std::string first = a.rest.empty() ? "" : Lower(a.rest[0]);
        bool touched = false;
        if (first == "on" || a.Yes("On", false) || (a.Has("Enabled") && a.Yes("Enabled", true))) { r.sun = true; touched = true; }
        else if (first == "off" || (a.Has("On") && !a.Yes("On", true)) || (a.Has("Enabled") && !a.Yes("Enabled", true))) { r.sun = false; touched = true; }
        else if (first == "toggle" || (a.kv.empty() && a.rest.empty())) { r.sun = !r.sun; touched = true; }
        if (a.Has("Azimuth")) { r.sun_azimuth = std::fmod(a.Num("Azimuth", r.sun_azimuth) + 360.0, 360.0); r.sun = true; touched = true; }
        if (a.Has("Altitude")) { r.sun_altitude = std::clamp(a.Num("Altitude", r.sun_altitude), -90.0, 90.0); r.sun = true; touched = true; }
        if (a.Has("Intensity")) { r.sun_intensity = static_cast<float>(std::max(0.0, a.Num("Intensity", r.sun_intensity))); touched = true; }
        if (a.Has("Color")) { Color c; if (ParseColor(a.Get("Color"), c)) r.sun_color = c; touched = true; }
        if (a.Has("Skylight")) { r.skylight = a.Yes("Skylight", true); touched = true; }
        if (touched) ctx.Doc().Touch();
        if (!ctx.ScriptMode() && a.kv.empty() && a.rest.empty()) ctx.App().Panels().lights = true;
        ctx.Print(std::string("Sun ") + (r.sun ? "on" : "off") + ": Azimuth=" + FormatNumber(r.sun_azimuth) + " Altitude=" + FormatNumber(r.sun_altitude) + " Intensity=" + FormatNumber(r.sun_intensity) + " Skylight=" + (r.skylight ? "Yes" : "No"));
      }));
  Reg(e, "Skylight", Immediate([](CommandContext& ctx) { RenderSettings& r = ctx.Doc().Render(); r.skylight = !r.skylight; ctx.Doc().Touch(); ctx.Print(std::string("Skylight ") + (r.skylight ? "on" : "off")); }));
  Reg(e, "GroundPlane", Immediate([](CommandContext& ctx) {
        RenderSettings& r = ctx.Doc().Render();
        Args a = TakeArgs(ctx);
        const std::string first = a.rest.empty() ? "" : Lower(a.rest[0]);
        if (first == "on" || a.Yes("On", false)) r.ground_plane = true;
        else if (first == "off" || (a.Has("On") && !a.Yes("On", true))) r.ground_plane = false;
        else if (first == "toggle" || (a.kv.empty() && a.rest.empty())) r.ground_plane = !r.ground_plane;
        if (a.Has("Height")) { r.ground_height = a.Num("Height", r.ground_height); r.ground_auto_height = false; r.ground_plane = true; }
        if (a.Has("AutoHeight")) r.ground_auto_height = a.Yes("AutoHeight", true);
        if (a.Has("Color")) { Color c; if (ParseColor(a.Get("Color"), c)) r.ground_color = c; else ctx.Warn("Color: use r,g,b (0-255)"); }
        if (a.Has("Shadows")) r.ground_shadows = a.Yes("Shadows", true);
        ctx.Doc().Touch();
        ctx.Print(std::string("GroundPlane ") + (r.ground_plane ? "on" : "off") + ": Height=" + (r.ground_auto_height ? "Automatic" : FormatNumber(r.ground_height)) + " Color=" + ColorText(r.ground_color) + " Shadows=" + (r.ground_shadows ? "Yes" : "No"));
      }));
  Reg(e, "Environments", Immediate([](CommandContext& ctx) {
        RenderSettings& r = ctx.Doc().Render();
        Args a = TakeArgs(ctx);
        const std::string first = a.rest.empty() ? "" : Lower(a.rest[0]);
        if (first == "solid" || first == "gradient" || first == "sky" || a.Has("Background")) {
          const std::string b = a.Has("Background") ? Lower(a.Get("Background")) : first;
          if (b == "solid") r.background = RenderSettings::Background::Solid;
          else if (b == "gradient") r.background = RenderSettings::Background::Gradient;
          else if (b == "sky") r.background = RenderSettings::Background::Sky;
          else if (b == "none") { r.background = RenderSettings::Background::Solid; r.background_color = Color::FromBytes(255, 255, 255); }
        }
        if (a.Has("Color")) { Color c; if (ParseColor(a.Get("Color"), c)) { r.background_color = c; r.background = RenderSettings::Background::Solid; } }
        if (a.Has("Image")) r.environment_image = a.Get("Image");
        ctx.Doc().Touch();
        if (a.kv.empty() && a.rest.empty()) ctx.App().Panels().environments = true;
        const char* names[] = {"Solid", "Gradient", "Sky"};
        ctx.Print(std::string("Environment: background ") + names[static_cast<int>(r.background)] + (r.background == RenderSettings::Background::Solid ? " " + ColorText(r.background_color) : "") + (r.environment_image.empty() ? "" : " (image " + r.environment_image + ": Partial, not drawn yet)"));
      }), CommandStatus::Partial, "Colour, gradient and sky backgrounds; image environments are planned.");
  Reg(e, "Textures", Immediate([](CommandContext& ctx) { ctx.App().Panels().textures = true; }), CommandStatus::Partial, "Lists material textures; procedural textures are planned.");
  Reg(e, "Materials", Immediate([](CommandContext& ctx) { ctx.App().Panels().materials = true; }));
  Reg(e, "MaterialEditor", Immediate([](CommandContext& ctx) { ctx.App().Panels().materials = true; }));
  Reg(e, "RenderAssignMaterialToObjects", Make<AssignMaterialCommand>(false));
  Reg(e, "RenderAssignMaterialToLayersOfObjects", Make<AssignMaterialCommand>(true));
  Reg(e, "RenderMergeIdenticalMaterials", Immediate([](CommandContext& ctx) {
        Document& doc = ctx.Doc();
        std::vector<Material>& mats = doc.Materials();
        std::map<std::string, std::string> rename;
        for (size_t i = 0; i < mats.size(); ++i) {
          if (rename.count(mats[i].name)) continue;
          for (size_t j = i + 1; j < mats.size(); ++j) {
            if (!rename.count(mats[j].name) && mats[i].SameAppearance(mats[j])) rename[mats[j].name] = mats[i].name;
          }
        }
        if (rename.empty()) { ctx.Print("RenderMergeIdenticalMaterials: no identical materials"); return; }
        doc.BeginChange("RenderMergeIdenticalMaterials");
        for (SceneObject& o : doc.Objects()) { auto it = rename.find(o.material_name); if (it != rename.end()) o.material_name = it->second; }
        for (Layer& l : doc.Layers()) { auto it = rename.find(l.material); if (it != rename.end()) l.material = it->second; }
        for (const auto& [from, to] : rename) doc.RemoveMaterial(from);
        ctx.Print("RenderMergeIdenticalMaterials: merged " + std::to_string(rename.size()) + " material(s)");
      }));
  Reg(e, "AssignBlankTexture", OnSelection("Select objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        // A neutral 64 x 64 checker written next to the settings so the
        // texture pipeline can be tried without hunting for an image.
        const std::string dir = ConfigDirectory();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::string path = dir + "/blank_texture.ppm";
        if (!std::filesystem::exists(path, ec)) {
          std::vector<unsigned char> rgb(64 * 64 * 3);
          for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const bool a = ((x / 8) + (y / 8)) % 2 == 0;
            unsigned char* p = &rgb[(static_cast<size_t>(y) * 64 + x) * 3];
            p[0] = p[1] = p[2] = a ? 235 : 180;
          }
          std::string err;
          if (!SaveImageRGB(path, 64, 64, rgb, err)) { ctx.Warn(err); return; }
        }
        ctx.Doc().BeginChange("AssignBlankTexture");
        int n = 0;
        for (ObjectId id : ids) {
          SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          Material& m = OwnMaterial(ctx.Doc(), *o);
          m.texture_path = path;
          if (m.mapping == TextureMapping::Default) m.mapping = TextureMapping::Surface;
          ++n;
        }
        ctx.App().Renderer().RefreshTextures();
        ctx.Print("AssignBlankTexture: " + path + " assigned to " + std::to_string(n) + " object(s)");
      }));
  Reg(e, "ApplyPlanarMapping", Make<MappingCommand>(TextureMapping::Planar));
  Reg(e, "ApplyBoxMapping", Make<MappingCommand>(TextureMapping::Box));
  Reg(e, "ApplyCylindricalMapping", Make<MappingCommand>(TextureMapping::Cylindrical));
  Reg(e, "ApplySphericalMapping", Make<MappingCommand>(TextureMapping::Spherical));
  Reg(e, "ApplySurfaceMapping", Make<MappingCommand>(TextureMapping::Surface));
  Reg(e, "ApplyCustomMapping", Make<MappingCommand>(TextureMapping::Custom), CommandStatus::Partial, "Planar projection in the object's box; custom source objects are planned.");
  Reg(e, "MappingWidget", Immediate([](CommandContext& ctx) { ctx.Print("MappingWidget: mapping projections use the object's bounding box; interactive widgets are planned. Use ApplyPlanarMapping Scale=n to tile."); }), CommandStatus::Partial);
  Reg(e, "MappingWidgetOff", Immediate([](CommandContext& ctx) { ctx.Print("MappingWidgetOff: no mapping widgets are shown."); }), CommandStatus::Partial);
  Reg(e, "RemoveMappingChannel", OnSelection("Select objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("RemoveMappingChannel");
        int n = 0;
        for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->mapping = TextureMapping::Default; o->mapping_scale = 1.f; o->InvalidateDisplay(); ++n; }
        ctx.Print("RemoveMappingChannel: " + std::to_string(n) + " object(s) use their material's mapping again");
      }));
  Reg(e, "MatchMapping", Make<MatchMappingCommand>());
  Reg(e, "PackTextures", Immediate([](CommandContext& ctx) { ctx.Print("PackTextures: textures are referenced by path; embedding them in the .3dm is planned. " + std::to_string(ImageFiles(ctx.Doc()).size()) + " image file(s) referenced."); }), CommandStatus::Partial);
  Reg(e, "UnpackTextures", Immediate([](CommandContext& ctx) { ctx.Print("UnpackTextures: textures are referenced by path; nothing to unpack."); }), CommandStatus::Partial);
  Reg(e, "RefreshAllTextures", Immediate([](CommandContext& ctx) { ctx.App().Renderer().RefreshTextures(); for (SceneObject& o : ctx.Doc().Objects()) o.InvalidateDisplay(); ctx.Print("RefreshAllTextures: texture cache cleared, images reload on the next frame"); }));
  Reg(e, "DownloadLibraryTextures", Immediate([](CommandContext& ctx) { ctx.Print("DownloadLibraryTextures: Dino 8 does not download anything. Point a material at any BMP/PPM/PNG file instead."); }), CommandStatus::Partial);
  Reg(e, "SetPerFaceColorByFacePack", Immediate([](CommandContext& ctx) { ctx.Print("SetPerFaceColorByFacePack: per-face colours are planned; materials apply per object."); }), CommandStatus::Partial);
  Reg(e, "RemovePerFaceColors", Immediate([](CommandContext& ctx) { ctx.Print("RemovePerFaceColors: no per-face colours are stored."); }), CommandStatus::Partial);
  Reg(e, "SynchronizeRenderColors", OnSelection("Select objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("SynchronizeRenderColors");
        int n = 0;
        for (ObjectId id : ids) {
          SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          Color c = ctx.Doc().EffectiveColor(*o);
          // A black by-layer colour (Rhino's default layer) becomes the neutral default, like MaterialFor.
          if (c.r + c.g + c.b < 0.05f && o->color_by_layer) c = Color::FromBytes(200, 200, 200);
          Material& m = OwnMaterial(ctx.Doc(), *o);
          m.diffuse = c;
          o->InvalidateDisplay();
          ++n;
        }
        ctx.Print("SynchronizeRenderColors: " + std::to_string(n) + " material(s) now match the display colour");
      }));

  // ---- render commands --------------------------------------------------
  Reg(e, "Render", Immediate([](CommandContext& ctx) {
        Args a = TakeArgs(ctx);
        int w = static_cast<int>(a.Num("Width", 0)), h = static_cast<int>(a.Num("Height", 0));
        if (a.rest.size() >= 2) { w = std::atoi(a.rest[0].c_str()); h = std::atoi(a.rest[1].c_str()); }
        const int q = static_cast<int>(a.Num("Quality", 0));
        DoRender(ctx, w, h, q, false, "Render");
      }));
  auto preview = [](CommandContext& ctx) {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) return;
    DoRender(ctx, std::max(vp->Width(), 16), std::max(vp->Height(), 16), 1, false, "RenderPreview");
  };
  Reg(e, "RenderPreview", Immediate(preview));
  Reg(e, "RenderPreviewInWindow", Immediate(preview));
  Reg(e, "RenderPreviewWindow", Immediate(preview));
  Reg(e, "RenderArctic", Immediate([](CommandContext& ctx) { DoRender(ctx, 0, 0, 0, true, "RenderArctic"); }));
  Reg(e, "RenderWindow", Immediate([](CommandContext& ctx) { ctx.App().Panels().render_window = true; }));
  Reg(e, "CloseRenderWindow", Immediate([](CommandContext& ctx) { ctx.App().CloseRenderWindow(); }));
  Reg(e, "SaveRenderWindowAs", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        auto save = [&app, &ctx](const std::string& path) {
          std::string err;
          const std::string p = WithImageExt(path);
          if (app.SaveLastRender(p, err)) ctx.Print("Saved rendering " + p + " (" + std::to_string(app.LastRender().width) + " x " + std::to_string(app.LastRender().height) + ")");
          else ctx.Warn(err);
        };
        if (auto p = ctx.Engine().TakePendingInput()) { save(*p); return; }
        if (!app.LastRender().Valid()) { ctx.Warn("Nothing has been rendered yet (run Render first)"); return; }
        app.ShowFileDialog("Save rendering", {".bmp", ".ppm"}, true, [save](const std::string& path) { save(path); });
      }));
  Reg(e, "CopyRenderWindowToClipboard", Immediate([](CommandContext& ctx) {
        std::string err;
        const std::string path = ConfigDirectory() + "/last_render.bmp";
        std::error_code ec;
        std::filesystem::create_directories(ConfigDirectory(), ec);
        if (ctx.App().SaveLastRender(path, err)) ctx.Print("CopyRenderWindowToClipboard: no image clipboard yet; the rendering was written to " + path);
        else ctx.Warn(err);
      }), CommandStatus::Partial, "Writes the image to a file next to the settings instead of the clipboard.");
  Reg(e, "RenderOpenLastRendering", Immediate([](CommandContext& ctx) {
        ctx.App().Panels().render_window = true;
        const RenderImage& img = ctx.App().LastRender();
        if (img.Valid()) ctx.Print("RenderOpenLastRendering: showing the last rendering" + (img.last_saved_path.empty() ? std::string() : " (saved as " + img.last_saved_path + ")"));
        else ctx.Print("RenderOpenLastRendering: nothing rendered in this session yet");
      }), CommandStatus::Partial, "Shows the last in-session rendering; opening image files is planned.");
  Reg(e, "RenderOpenRenderImage", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        auto open = [&app, &ctx](const std::string& path) {
          Image img; std::string err;
          if (!LoadImageFile(path, img, err)) { ctx.Warn(err); return; }
          RenderImage& r = app.LastRender();
          if (r.texture) app.Renderer().DeleteTexture(r.texture);
          r = RenderImage{};
          r.width = img.width; r.height = img.height; r.view_name = std::filesystem::path(path).filename().string();
          r.rgb.resize(static_cast<size_t>(img.width) * img.height * 3);
          for (size_t i = 0; i < static_cast<size_t>(img.width) * img.height; ++i) { r.rgb[i * 3] = img.rgba[i * 4]; r.rgb[i * 3 + 1] = img.rgba[i * 4 + 1]; r.rgb[i * 3 + 2] = img.rgba[i * 4 + 2]; }
          r.texture = app.Renderer().CreateTexture(r.width, r.height, r.rgb.data(), 3);
          app.Panels().render_window = true;
          ctx.Print("RenderOpenRenderImage: opened " + path + " (" + std::to_string(img.width) + " x " + std::to_string(img.height) + ")");
        };
        if (auto p = ctx.Engine().TakePendingInput()) { open(*p); return; }
        app.ShowFileDialog("Open rendering", {".bmp", ".ppm", ".pgm", ".png"}, false, open);
      }));
  Reg(e, "RenderBlowup", Make<RenderBlowupCommand>(), CommandStatus::Partial, "Crops a full-view render to the picked region.");
  Reg(e, "Rendering", Immediate([](CommandContext& ctx) { ctx.App().Panels().rendering = true; }));
  Reg(e, "RenderSettings", Immediate([](CommandContext& ctx) { ctx.App().Panels().rendering = true; }));
  Reg(e, "SetCurrentRenderPlugIn", Immediate([](CommandContext& ctx) { ctx.Print("Current renderer: Dino 8 built-in renderer (OpenGL Blinn-Phong, no plug-ins needed)"); }));
  Reg(e, "SetActiveRenderer", Immediate([](CommandContext& ctx) { ctx.Print("Current renderer: Dino 8 built-in renderer"); }));
  Reg(e, "RenderReportImageFiles", Immediate([](CommandContext& ctx) {
        const auto files = ImageFiles(ctx.Doc());
        ctx.Print("RenderReportImageFiles: " + std::to_string(files.size()) + " image file(s) referenced");
        for (const auto& [p, ok] : files) ctx.Print(std::string("  ") + (ok ? "found   " : "missing ") + p);
      }));
  Reg(e, "RenderReportMissingImageFiles", Immediate([](CommandContext& ctx) {
        int missing = 0;
        for (const auto& [p, ok] : ImageFiles(ctx.Doc())) if (!ok) { ++missing; ctx.Print("  missing " + p); }
        ctx.Print("RenderReportMissingImageFiles: " + std::to_string(missing) + " missing image file(s)");
      }));
  Reg(e, "BatchRenderNamedViews", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        auto run = [&app, &ctx, vp](const std::string& folder) {
          std::error_code ec;
          std::filesystem::create_directories(folder, ec);
          Document& doc = ctx.Doc();
          if (doc.NamedViews().empty()) { ctx.Warn("BatchRenderNamedViews: the document has no named views"); return; }
          const CameraState saved = vp->GetCamera().State();
          int n = 0;
          for (const NamedView& nv : doc.NamedViews()) {
            vp->GetCamera().SetState(nv.camera);
            std::string err;
            if (!app.RenderView(vp, 0, 0, 0, false, err)) { ctx.Warn(nv.name + ": " + err); continue; }
            std::string file = nv.name;
            for (char& c : file) if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
            const std::string path = folder + "/" + file + ".bmp";
            if (app.SaveLastRender(path, err)) { ctx.Print("  " + nv.name + " -> " + path); ++n; } else ctx.Warn(err);
          }
          vp->GetCamera().SetState(saved);
          ctx.Print("BatchRenderNamedViews: " + std::to_string(n) + " view(s) rendered to " + folder);
        };
        if (auto p = ctx.Engine().TakePendingInput()) { run(*p); return; }
        app.ShowFileDialog("Folder for the renderings (pick any file name in it)", {".bmp"}, true, [run](const std::string& path) { run(std::filesystem::path(path).parent_path().string()); });
      }));
  Reg(e, "ShadeSelected", OnSelection("Select objects to shade", [](CommandContext& ctx, const std::vector<ObjectId>&) {
        if (Viewport* vp = ctx.ActiveViewport()) if (vp->Mode() == DisplayMode::Wireframe) vp->SetMode(DisplayMode::Shaded);
        ctx.Print("ShadeSelected: the viewport is shaded; per-object display modes are planned.");
      }), CommandStatus::Partial, "Shades the whole viewport.");
  auto render_mesh_note = [](CommandContext& ctx, const char* name) {
    ctx.Print(std::string(name) + ": render meshes are built automatically for shaded and rendered views (see PolygonCount / ExtractRenderMesh).");
  };
  Reg(e, "ToggleRenderMesh", Immediate([render_mesh_note](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->SetMode(vp->Mode() == DisplayMode::Wireframe ? DisplayMode::Shaded : DisplayMode::Wireframe); render_mesh_note(ctx, "ToggleRenderMesh"); }), CommandStatus::Partial, "Toggles the viewport between Wireframe and Shaded.");
  Reg(e, "ShowRenderMesh", Immediate([render_mesh_note](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) if (vp->Mode() == DisplayMode::Wireframe) vp->SetMode(DisplayMode::Shaded); render_mesh_note(ctx, "ShowRenderMesh"); }), CommandStatus::Partial);
  Reg(e, "HideRenderMesh", Immediate([render_mesh_note](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->SetMode(DisplayMode::Wireframe); render_mesh_note(ctx, "HideRenderMesh"); }), CommandStatus::Partial);
  Reg(e, "ExtractRenderMesh", OnSelection("Select surfaces, polysurfaces or SubDs", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<SceneObject> made;
        size_t triangles = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind == ObjectKind::Point || o->kind == ObjectKind::Curve) continue;
          o->EnsureDisplay(ctx.App().curve_display_tolerance, ctx.App().surface_display_tolerance);
          const std::vector<float>& tri = o->Display().triangles;
          if (tri.empty()) continue;
          SceneObject m = SceneObject::MakeMesh(MeshFromTriangles(tri));
          m.name = o->name.empty() ? "Render mesh" : o->name + " render mesh";
          m.layer_index = o->layer_index;
          m.material_name = o->material_name;
          triangles += tri.size() / 18;
          made.push_back(std::move(m));
        }
        if (made.empty()) { ctx.Warn("ExtractRenderMesh: nothing selected has a render mesh"); return; }
        ctx.Doc().BeginChange("ExtractRenderMesh");
        ctx.Doc().SelectNone();
        for (SceneObject& m : made) { const ObjectId nid = ctx.Doc().Add(std::move(m)); ctx.Doc().Select(nid, true); }
        ctx.Print("ExtractRenderMesh: " + std::to_string(made.size()) + " mesh(es), " + std::to_string(triangles) + " triangles");
      }));
  Reg(e, "SetMeshSurfaceParameters", Immediate([](CommandContext& ctx) { ctx.App().Panels().options = true; ctx.Print("SetMeshSurfaceParameters: adjust the surface display tolerance in Options (finer = more polygons)."); }), CommandStatus::Partial, "Uses the global display tolerance.");
  Reg(e, "PolygonCount", Immediate([](CommandContext& ctx) {
        size_t tri = 0, objects = 0;
        const bool any_selected = ctx.Doc().SelectedCount() > 0;
        for (const SceneObject& o : ctx.Doc().Objects()) {
          if (any_selected ? !o.selected : !ctx.Doc().IsObjectVisible(o)) continue;
          o.EnsureDisplay(ctx.App().curve_display_tolerance, ctx.App().surface_display_tolerance);
          const size_t t = o.Display().triangles.size() / 18;
          if (t == 0) continue;
          tri += t;
          ++objects;
        }
        ctx.Print("PolygonCount: " + std::to_string(tri) + " triangles in " + std::to_string(objects) + (any_selected ? " selected object(s)" : " visible object(s)"));
      }));
  Reg(e, "GradientView", Immediate([](CommandContext& ctx) {
        RenderSettings& r = ctx.Doc().Render();
        Args a = TakeArgs(ctx);
        const std::string first = a.rest.empty() ? "" : Lower(a.rest[0]);
        if (first == "on") r.gradient_view = true; else if (first == "off") r.gradient_view = false; else r.gradient_view = !r.gradient_view;
        ctx.Doc().Touch();
        ctx.Print(std::string("GradientView ") + (r.gradient_view ? "on" : "off"));
      }));
  Reg(e, "BackgroundBitmap", Immediate([](CommandContext& ctx) { ctx.Print("BackgroundBitmap: viewport background images are planned; use Picture to place an image as a textured plane."); }), CommandStatus::Partial);
  Reg(e, "Picture", Make<PictureCommand>(), CommandStatus::Partial, "Places a BMP/PPM/PNG image on a plane; PictureFrame options are planned.");
  Reg(e, "Bake", Immediate([](CommandContext& ctx) { ctx.Print("Bake: baking textures is planned."); }), CommandStatus::Partial);
  Reg(e, "BakeMapping", Immediate([](CommandContext& ctx) { ctx.Print("BakeMapping: baking mappings is planned; use ExtractRenderMesh for the display mesh."); }), CommandStatus::Partial);
}

}  // namespace dino8::app
