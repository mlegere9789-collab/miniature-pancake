// View tools: clipping planes (real GPU clipping + section curves), layouts
// and details (paper space), named / remapped / auto-aligned CPlanes,
// camera animation (turntable, path, fly-through, playback and recording)
// and viewport list management.
//
// Options are plain command-line tokens (Name=Value) so every command can
// be scripted: "Layout Name=Sheet1 Width=297 Height=210",
// "SetTurntableAnimation Frames=36 Degrees=360", "NamedCPlane Save Front".
#include "commands/cmd_common.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "app/ViewTools.h"
#include "io/File3dm.h"

namespace dino8::app {

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Consumes every pending command-line token: "Key=Value" tokens fill `opts`
// (lower-case keys), the rest are returned in order.
std::vector<std::string> TakeOptions(CommandContext& ctx, std::map<std::string, std::string>& opts) {
  std::vector<std::string> positional;
  while (std::optional<std::string> tok = ctx.Engine().TakePendingInput()) {
    const size_t eq = tok->find('=');
    if (eq != std::string::npos && eq > 0) opts[ToLower(tok->substr(0, eq))] = tok->substr(eq + 1);
    else positional.push_back(*tok);
  }
  return positional;
}

double NumberOr(const std::map<std::string, std::string>& opts, const char* key, double def) {
  auto it = opts.find(key);
  if (it == opts.end()) return def;
  char* end = nullptr;
  const double v = std::strtod(it->second.c_str(), &end);
  return (end && end != it->second.c_str()) ? v : def;
}

std::string StringOr(const std::map<std::string, std::string>& opts, const char* key, const std::string& def) {
  auto it = opts.find(key);
  return it == opts.end() ? def : it->second;
}

bool IsYes(const std::string& v) { const std::string l = ToLower(v); return l == "yes" || l == "on" || l == "true" || l == "1"; }

ConstructionPlane CPlaneFromPlane(const ON_Plane& pl) {
  ConstructionPlane cp;
  cp.origin = pl.origin; cp.x_axis = pl.xaxis; cp.y_axis = pl.yaxis;
  return cp;
}

ON_Plane PlaneOf(const ConstructionPlane& cp) { return ON_Plane(cp.origin, cp.x_axis, cp.y_axis); }

bool SameCPlane(const ConstructionPlane& a, const ConstructionPlane& b) {
  return a.origin.DistanceTo(b.origin) < 1e-9 && (a.x_axis - b.x_axis).Length() < 1e-9 && (a.y_axis - b.y_axis).Length() < 1e-9;
}

std::string Describe(const ConstructionPlane& cp) {
  return "origin " + FormatPoint(cp.origin) + " x " + FormatPoint(Point3d(cp.x_axis)) + " y " + FormatPoint(Point3d(cp.y_axis));
}

// Best plane of an object: planar curves/surfaces, a planar brep face (the
// one facing `toward` when given), a surface's mid-point tangent plane.
bool PlaneOfObject(const SceneObject& o, ON_Plane& out, std::optional<Vector3d> toward = std::nullopt) {
  const double tol = 1e-6;
  if (o.kind == ObjectKind::Curve && o.curve) {
    if (o.curve->raw().IsPlanar(&out, tol)) return true;
    const kernel::Interval d = o.curve->Domain();
    out = ON_Plane(o.curve->PointAt(d.min), o.curve->TangentAt(d.min));
    return true;
  }
  if (o.kind == ObjectKind::Surface && o.surface) {
    if (o.surface->raw().IsPlanar(&out, tol)) return true;
    const kernel::Interval du = o.surface->Domain(0), dv = o.surface->Domain(1);
    const double u = (du.min + du.max) / 2, v = (dv.min + dv.max) / 2;
    out = ON_Plane(o.surface->PointAt(u, v), o.surface->NormalAt(u, v));
    return true;
  }
  if (o.kind == ObjectKind::Brep && o.brep) {
    const ON_Brep& b = o.brep->raw();
    bool found = false;
    double best = -2;
    for (int i = 0; i < b.m_F.Count(); ++i) {
      const ON_BrepFace& f = b.m_F[i];
      ON_Plane pl;
      if (!f.IsPlanar(&pl, tol)) continue;
      if (f.m_bRev) pl.Flip();
      // Origin at the face centre.
      ON_BoundingBox bb;
      f.GetBoundingBox(bb);
      pl.origin = pl.ClosestPointTo(bb.Center());
      pl.UpdateEquation();
      const double score = toward ? ON_DotProduct(pl.zaxis, *toward) : 0.0;
      if (!found || score > best) { out = pl; best = score; found = true; }
    }
    if (found) return true;
    if (b.m_F.Count() > 0) {
      const ON_BrepFace& f = b.m_F[0];
      ON_Interval du = f.Domain(0), dv = f.Domain(1);
      out = ON_Plane(f.PointAt(du.Mid(), dv.Mid()), f.NormalAt(du.Mid(), dv.Mid()));
      return true;
    }
  }
  return false;
}

// Copied from cmd_curves2.cpp (file-local there): slices a mesh with a
// plane and chains the segments into polylines.
std::vector<std::vector<Point3d>> SliceMesh(const ON_Mesh& m, const ON_Plane& plane, double tol) {
  std::vector<std::pair<Point3d, Point3d>> segs;
  const int fc = m.FaceCount();
  for (int fi = 0; fi < fc; ++fi) {
    const ON_MeshFace& f = m.m_F[fi];
    const int n = f.IsTriangle() ? 3 : 4;
    for (int tri = 0; tri < (n == 4 ? 2 : 1); ++tri) {
      int idx[3] = {f.vi[0], f.vi[tri + 1], f.vi[tri + 2]};
      Point3d p[3];
      double d[3];
      for (int k = 0; k < 3; ++k) { p[k] = m.Vertex(idx[k]); d[k] = plane.DistanceTo(p[k]); }
      std::vector<Point3d> hits;
      for (int k = 0; k < 3; ++k) {
        const int j = (k + 1) % 3;
        if ((d[k] < 0 && d[j] >= 0) || (d[k] >= 0 && d[j] < 0)) {
          double t = d[k] / (d[k] - d[j]);
          hits.push_back(p[k] + (p[j] - p[k]) * t);
        }
      }
      if (hits.size() == 2 && hits[0].DistanceTo(hits[1]) > tol) segs.emplace_back(hits[0], hits[1]);
    }
  }
  std::vector<std::vector<Point3d>> out;
  std::vector<bool> used(segs.size(), false);
  for (size_t i = 0; i < segs.size(); ++i) {
    if (used[i]) continue;
    used[i] = true;
    std::vector<Point3d> pl = {segs[i].first, segs[i].second};
    bool grew = true;
    while (grew) {
      grew = false;
      for (size_t j = 0; j < segs.size(); ++j) {
        if (used[j]) continue;
        if (segs[j].first.DistanceTo(pl.back()) <= tol) { pl.push_back(segs[j].second); used[j] = true; grew = true; }
        else if (segs[j].second.DistanceTo(pl.back()) <= tol) { pl.push_back(segs[j].first); used[j] = true; grew = true; }
        else if (segs[j].second.DistanceTo(pl.front()) <= tol) { pl.insert(pl.begin(), segs[j].first); used[j] = true; grew = true; }
        else if (segs[j].first.DistanceTo(pl.front()) <= tol) { pl.insert(pl.begin(), segs[j].second); used[j] = true; grew = true; }
      }
    }
    out.push_back(std::move(pl));
  }
  return out;
}

// ---------------------------------------------------------------------------
// Clipping planes
// ---------------------------------------------------------------------------

// Planes a command should act on: named ones, else the selected ones, else all.
std::vector<ClippingPlane*> TargetPlanes(CommandContext& ctx, const std::vector<std::string>& names, bool enabled_only = false) {
  std::vector<ClippingPlane*> out;
  for (ClippingPlane& cp : ctx.Doc().ClippingPlanes()) {
    bool take = names.empty() ? cp.selected : false;
    for (const std::string& n : names) if (ToLower(n) == ToLower(cp.name) || n == std::to_string(cp.id)) take = true;
    if (take && (!enabled_only || cp.enabled)) out.push_back(&cp);
  }
  if (out.empty() && names.empty()) {
    for (ClippingPlane& cp : ctx.Doc().ClippingPlanes()) if (!enabled_only || cp.enabled) out.push_back(&cp);
  }
  return out;
}

class ClippingPlaneCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    viewports_ = "All";
    options = {{"Viewports", viewports_, {"All", "Active"}, false, false}, {"Name", "", {}, false, false}};
    if (!ctx.ActiveViewport()) { ctx.Warn("No active viewport"); Finish(); return; }
    WantPoint("First corner of clipping plane");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Viewports") { viewports_ = ToLower(v) == "active" ? "Active" : "All"; options[0].value = viewports_; }
    else if (n == "Name") name_ = v;
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() < 2) { WantPoint("Other corner of clipping plane"); return; }
    ctx.ClearPreview();
    const ON_Plane pl = ActivePlane(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(pts_[0], &u0, &v0);
    pl.ClosestPointTo(pts_[1], &u1, &v1);
    ClippingPlane cp;
    // Keep the corners' elevation above the CPlane (typed points may sit off it).
    cp.origin = pl.PointAt((u0 + u1) / 2, (v0 + v1) / 2) + pl.zaxis * ON_DotProduct(pts_[0] - pl.origin, pl.zaxis);
    cp.x_axis = pl.xaxis;
    cp.y_axis = pl.yaxis;
    cp.width = std::max(std::fabs(u1 - u0), 0.1);
    cp.height = std::max(std::fabs(v1 - v0), 0.1);
    cp.name = name_;
    if (viewports_ == "Active" && ctx.ActiveViewport()) cp.viewports = {ctx.ActiveViewport()->Name()};
    ctx.Doc().BeginChange("ClippingPlane");
    for (ClippingPlane& o : ctx.Doc().ClippingPlanes()) o.selected = false;
    const int id = ctx.Doc().AddClippingPlane(cp);
    ClippingPlane* added = ctx.Doc().FindClippingPlane(id);
    added->selected = true;
    ctx.Print("ClippingPlane: created " + added->name + " (" + FormatNumber(cp.width) + " x " + FormatNumber(cp.height) + ", normal " +
              FormatPoint(Point3d(cp.Normal())) + ", clips " + (cp.viewports.empty() ? std::string("all viewports") : cp.viewports.front()) + ")");
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.empty()) return;
    ctx.ClearPreview();
    const ON_Plane pl = ActivePlane(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(pts_[0], &u0, &v0); pl.ClosestPointTo(h, &u1, &v1);
    ctx.AddPreviewPolyline({pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)}, true);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<Point3d> pts_;
  std::string viewports_, name_;
};

void SetPlanesEnabled(CommandContext& ctx, bool on) {
  std::map<std::string, std::string> opts;
  const std::vector<std::string> names = TakeOptions(ctx, opts);
  std::vector<ClippingPlane*> planes = TargetPlanes(ctx, names);
  if (planes.empty()) { ctx.Warn("No clipping planes"); return; }
  ctx.Doc().BeginChange(on ? "EnableClippingPlane" : "DisableClippingPlane");
  for (ClippingPlane* p : planes) p->enabled = on;
  ctx.Doc().Touch();
  ctx.Print(std::string(on ? "EnableClippingPlane" : "DisableClippingPlane") + ": " + std::to_string(planes.size()) + " plane(s) " + (on ? "enabled" : "disabled"));
}

// Section curves of the visible (or selected) objects with each plane.
int AddSections(CommandContext& ctx, const std::vector<ClippingPlane*>& planes, const std::vector<ObjectId>& ids) {
  int made = 0;
  const double tol = ctx.Settings().absolute_tolerance * 10;
  std::vector<SceneObject> added;
  for (ClippingPlane* cp : planes) {
    const ON_Plane plane(cp->origin, cp->x_axis, cp->y_axis);
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->user_text.count("ClippingSection")) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, 0.005);
      if (!m) continue;
      for (std::vector<Point3d> pl : SliceMesh(m->raw(), plane, tol)) {
        if (pl.size() < 2) continue;
        // A chain that comes back to its start is a closed section loop.
        // (Mesh seams leave a small gap between the chain's ends.)
        if (pl.size() > 2 && pl.front().DistanceTo(pl.back()) <= tol * 10) pl.back() = pl.front();
        SceneObject c = SceneObject::MakeCurve(PolylineCurve(pl));
        c.layer_index = o->layer_index;
        c.user_text["ClippingSection"] = cp->name;
        c.name = cp->name + " section";
        added.push_back(std::move(c));
        ++made;
      }
    }
  }
  for (SceneObject& c : added) ctx.Doc().Add(std::move(c));
  return made;
}

void ClippingSections(CommandContext& ctx, const char* label) {
  std::map<std::string, std::string> opts;
  const std::vector<std::string> names = TakeOptions(ctx, opts);
  std::vector<ClippingPlane*> planes = TargetPlanes(ctx, names, true);
  if (planes.empty()) { ctx.Warn(std::string(label) + ": no enabled clipping planes"); return; }
  std::vector<ObjectId> ids = ctx.Doc().SelectedIds();
  if (ids.empty()) for (const SceneObject& o : ctx.Doc().Objects()) if (ctx.Doc().IsObjectVisible(o)) ids.push_back(o.id);
  ctx.Doc().BeginChange(label);
  const int made = AddSections(ctx, planes, ids);
  ctx.Print(std::string(label) + ": " + std::to_string(made) + " curve(s) from " + std::to_string(planes.size()) + " plane(s)");
}

CameraState SectionCamera(const ClippingPlane& cp, const CameraState& base) {
  CameraState c = base;
  const Vector3d n = cp.Normal();
  const double dist = std::max(cp.width, cp.height) * 2.0;
  c.target = cp.origin;
  c.eye = cp.origin + n * dist;
  c.up = std::fabs(ON_DotProduct(n, Vector3d(0, 0, 1))) > 0.99 ? Vector3d(0, 1, 0) : Vector3d(0, 0, 1);
  c.perspective = false;
  c.ortho_height = std::max(cp.width, cp.height) * 1.2;
  return c;
}

// ---------------------------------------------------------------------------
// Layouts and details
// ---------------------------------------------------------------------------

std::string UniqueLayoutName(Document& doc, const std::string& base) {
  std::string name = base;
  for (int i = 2; doc.FindLayout(name); ++i) name = base + " " + std::to_string(i);
  return name;
}

class LayoutCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    std::map<std::string, std::string> opts;
    const std::vector<std::string> pos = TakeOptions(ctx, opts);
    width_ = NumberOr(opts, "width", 297);
    height_ = NumberOr(opts, "height", 210);
    std::string name = StringOr(opts, "name", pos.empty() ? "" : pos[0]);
    if (!name.empty() || ctx.ScriptMode() || !pos.empty()) { Create(ctx, name); Finish(); return; }
    WantText("Layout name", "Layout " + std::to_string(ctx.Doc().Layouts().size() + 1));
  }
  void OnText(CommandContext& ctx, const std::string& t) override { Create(ctx, t); Finish(); }
  void OnEnter(CommandContext& ctx) override { Create(ctx, ""); Finish(); }
  void Create(CommandContext& ctx, std::string name) {
    if (name.empty()) name = "Layout " + std::to_string(ctx.Doc().Layouts().size() + 1);
    name = UniqueLayoutName(ctx.Doc(), name);
    ctx.Doc().BeginChange("Layout");
    Layout L;
    L.name = name;
    L.width_mm = std::max(10.0, width_);
    L.height_mm = std::max(10.0, height_);
    ctx.Doc().Layouts().push_back(L);
    ctx.Doc().Touch();
    ctx.App().SetActiveLayout(static_cast<int>(ctx.Doc().Layouts().size()) - 1);
    ctx.Print("Layout: created " + name + " (" + FormatNumber(L.width_mm) + " x " + FormatNumber(L.height_mm) + " mm)");
  }
  double width_ = 297, height_ = 210;
};

// Details the detail commands act on: the active detail, else the selected
// details, else every detail of the active layout.
std::vector<LayoutDetail*> TargetDetails(CommandContext& ctx, const std::vector<std::string>& names = {}) {
  std::vector<LayoutDetail*> out;
  Layout* L = ctx.App().ActiveLayout();
  if (!L) return out;
  for (LayoutDetail& d : L->details) for (const std::string& n : names) if (ToLower(n) == ToLower(d.name)) out.push_back(&d);
  if (!out.empty()) return out;
  const int active = ctx.App().ActiveDetailIndex();
  if (active >= 0 && active < static_cast<int>(L->details.size())) { out.push_back(&L->details[static_cast<size_t>(active)]); return out; }
  for (LayoutDetail& d : L->details) if (d.selected) out.push_back(&d);
  if (out.empty()) for (LayoutDetail& d : L->details) out.push_back(&d);
  return out;
}

int DetailIndex(Layout& L, const LayoutDetail* d) {
  for (size_t i = 0; i < L.details.size(); ++i) if (&L.details[i] == d) return static_cast<int>(i);
  return -1;
}

CameraState DetailCamera(CommandContext& ctx, const std::string& view, double aspect, double scale, double height_mm) {
  Camera cam;
  if (Viewport* src = ctx.App().FindViewport(view)) cam = src->GetCamera();
  else if (view == "Perspective") cam.SetPerspective();
  else if (view == "Front") cam.SetFront();
  else if (view == "Right") cam.SetRight();
  else if (view == "Back") cam.SetBack();
  else if (view == "Left") cam.SetLeft();
  else if (view == "Bottom") cam.SetBottom();
  else cam.SetTop();
  kernel::BoundingBox box;
  if (!ctx.Doc().VisibleBoundingBox(box)) box = kernel::BoundingBox{Point3d(-20, -20, -20), Point3d(20, 20, 20)};
  cam.ZoomExtents(box, aspect);
  if (scale > 0 && !cam.State().perspective) cam.State().ortho_height = height_mm / scale;
  return cam.State();
}

class DetailCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (!ctx.App().ActiveLayout()) { ctx.Warn("Detail: switch to a layout first (Layout / Layouts)"); Finish(); return; }
    ctx.App().SetActiveDetail(-1);
    options = {{"View", "Top", {"Top", "Front", "Right", "Perspective", "Bottom", "Back", "Left"}, false, false}, {"Scale", "0", {}, true, false}, {"Name", "", {}, false, false}};
    WantPoint("First corner of detail (page mm)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "View") { view_ = v; options[0].value = v; }
    else if (n == "Scale") { scale_ = std::strtod(v.c_str(), nullptr); options[1].value = v; }
    else if (n == "Name") name_ = v;
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() < 2) { WantPoint("Other corner of detail"); return; }
    ctx.ClearPreview();
    Layout* L = ctx.App().ActiveLayout();
    if (!L) { Finish(); return; }
    LayoutDetail d;
    d.x = std::min(pts_[0].x, pts_[1].x);
    d.y = std::min(pts_[0].y, pts_[1].y);
    d.width = std::max(std::fabs(pts_[1].x - pts_[0].x), 1.0);
    d.height = std::max(std::fabs(pts_[1].y - pts_[0].y), 1.0);
    d.name = name_.empty() ? "Detail " + std::to_string(L->details.size() + 1) : name_;
    for (int i = 2; ctx.App().FindViewport(d.name); ++i) d.name = (name_.empty() ? "Detail" : name_) + " " + std::to_string(i);
    d.standard_view = view_;
    d.scale = std::max(0.0, scale_);
    d.display_mode = view_ == "Perspective" ? "Shaded" : "Wireframe";
    d.camera = DetailCamera(ctx, view_, d.width / d.height, d.scale, d.height);
    ctx.Doc().BeginChange("Detail");
    for (LayoutDetail& o : L->details) o.selected = false;
    d.selected = true;
    L->details.push_back(d);
    ctx.Doc().Touch();
    ctx.App().SyncDetailViewports();
    ctx.Print("Detail: added " + d.name + " (" + FormatNumber(d.width) + " x " + FormatNumber(d.height) + " mm, " + view_ + (d.scale > 0 ? ", scale " + FormatNumber(d.scale) : "") + ") to " + L->name);
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.empty()) return;
    ctx.ClearPreview();
    ctx.AddPreviewPolyline({pts_[0], Point3d(h.x, pts_[0].y, 0), h, Point3d(pts_[0].x, h.y, 0)}, true);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<Point3d> pts_;
  std::string view_ = "Top", name_;
  double scale_ = 0;
};

void CopyDetailCamera(CommandContext& ctx, bool detail_to_viewport) {
  std::map<std::string, std::string> opts;
  const std::vector<std::string> pos = TakeOptions(ctx, opts);
  Layout* L = ctx.App().ActiveLayout();
  if (!L) { ctx.Warn("No active layout"); return; }
  std::vector<LayoutDetail*> details = TargetDetails(ctx);
  if (details.empty()) { ctx.Warn("The layout has no details"); return; }
  LayoutDetail* d = details.front();
  const std::string vp_name = StringOr(opts, "viewport", pos.empty() ? "Perspective" : pos[0]);
  Viewport* vp = nullptr;
  for (auto& v : ctx.Viewports()) if (ToLower(v->Name()) == ToLower(vp_name)) vp = v.get();
  if (!vp) { ctx.Warn("No model viewport named " + vp_name); return; }
  Viewport* dv = ctx.App().DetailViewport(DetailIndex(*L, d));
  if (detail_to_viewport) {
    vp->GetCamera().SetState(dv ? dv->GetCamera().State() : d->camera);
    vp->SetMode(DisplayModeFromName(d->display_mode));
    ctx.Print("CopyDetailToViewport: " + d->name + " -> " + vp->Name());
  } else {
    ctx.Doc().BeginChange("CopyViewportToDetail");
    d->camera = vp->GetCamera().State();
    d->display_mode = DisplayModeName(vp->Mode());
    d->standard_view = vp->StandardView();
    if (dv) { dv->GetCamera().SetState(d->camera); dv->SetMode(vp->Mode()); }
    ctx.Doc().Touch();
    ctx.Print("CopyViewportToDetail: " + vp->Name() + " -> " + d->name);
  }
}

void HideShowInDetail(CommandContext& ctx, const std::vector<ObjectId>& ids, int mode) {  // 0 hide, 1 show all, 2 show selected
  std::vector<LayoutDetail*> details = TargetDetails(ctx);
  if (details.empty()) { ctx.Warn("No layout detail (switch to a layout and add a Detail)"); return; }
  ctx.Doc().BeginChange(mode == 0 ? "HideInDetail" : "ShowInDetail");
  int n = 0;
  for (LayoutDetail* d : details) {
    if (mode == 1) { n += static_cast<int>(d->hidden_objects.size()); d->hidden_objects.clear(); continue; }
    for (ObjectId id : ids) {
      auto it = std::find(d->hidden_objects.begin(), d->hidden_objects.end(), id);
      if (mode == 0 && it == d->hidden_objects.end()) { d->hidden_objects.push_back(id); ++n; }
      else if (mode == 2 && it != d->hidden_objects.end()) { d->hidden_objects.erase(it); ++n; }
    }
  }
  ctx.Doc().Touch();
  ctx.Print(std::string(mode == 0 ? "HideInDetail" : mode == 1 ? "ShowInDetail" : "ShowSelectedInDetail") + ": " + std::to_string(n) + " object(s) in " + std::to_string(details.size()) + " detail(s)");
}

void HideShowLayersInDetail(CommandContext& ctx, bool hide) {
  std::map<std::string, std::string> opts;
  std::vector<std::string> names = TakeOptions(ctx, opts);
  std::vector<LayoutDetail*> details = TargetDetails(ctx);
  if (details.empty()) { ctx.Warn("No layout detail (switch to a layout and add a Detail)"); return; }
  std::vector<int> layers;
  if (names.empty()) layers.push_back(ctx.Doc().CurrentLayer());
  for (const std::string& n : names) {
    const int idx = ctx.Doc().FindLayer(n);
    if (idx >= 0) layers.push_back(idx); else ctx.Warn("No layer named " + n);
  }
  ctx.Doc().BeginChange(hide ? "HideLayersInDetail" : "ShowLayersInDetail");
  int n = 0;
  for (LayoutDetail* d : details) {
    for (int li : layers) {
      auto it = std::find(d->hidden_layers.begin(), d->hidden_layers.end(), li);
      if (hide && it == d->hidden_layers.end()) { d->hidden_layers.push_back(li); ++n; }
      else if (!hide && it != d->hidden_layers.end()) { d->hidden_layers.erase(it); ++n; }
    }
  }
  ctx.Doc().Touch();
  ctx.Print(std::string(hide ? "HideLayersInDetail" : "ShowLayersInDetail") + ": " + std::to_string(n) + " layer(s) in " + std::to_string(details.size()) + " detail(s)");
}

// ---------------------------------------------------------------------------
// CPlanes
// ---------------------------------------------------------------------------

void SetCPlane(CommandContext& ctx, Viewport* vp, const ConstructionPlane& cp, const std::string& what) {
  if (!vp) return;
  vp->CPlane() = cp;
  ctx.Print("CPlane " + vp->Name() + ": " + what + " (" + Describe(cp) + ")");
}

class CPlaneCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (!ctx.ActiveViewport()) { Finish(); return; }
    options = {{"World", "", {}, false, false}, {"View", "", {}, false, false}, {"Object", "", {}, false, false},
               {"Surface", "", {}, false, false}, {"Curve", "", {}, false, false}, {"3Point", "", {}, false, false},
               {"Elevation", "", {}, true, false}, {"Rotate", "", {}, true, false}, {"Next", "", {}, false, false},
               {"Previous", "", {}, false, false}, {"Top", "", {}, false, false}, {"Front", "", {}, false, false},
               {"Right", "", {}, false, false}, {"Bottom", "", {}, false, false}, {"Back", "", {}, false, false},
               {"Left", "", {}, false, false}};
    WantPoint("CPlane origin");
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    ConstructionPlane cp = vp->CPlane();
    if (n == "World" || n == "Top") { SetCPlane(ctx, vp, ConstructionPlane{}, n); Finish(); return; }
    if (n == "Bottom") { cp = ConstructionPlane{}; cp.y_axis = Vector3d(0, -1, 0); SetCPlane(ctx, vp, cp, n); Finish(); return; }
    if (n == "Front") { cp = ConstructionPlane{}; cp.y_axis = Vector3d(0, 0, 1); SetCPlane(ctx, vp, cp, n); Finish(); return; }
    if (n == "Back") { cp = ConstructionPlane{}; cp.x_axis = Vector3d(-1, 0, 0); cp.y_axis = Vector3d(0, 0, 1); SetCPlane(ctx, vp, cp, n); Finish(); return; }
    if (n == "Right") { cp = ConstructionPlane{}; cp.x_axis = Vector3d(0, 1, 0); cp.y_axis = Vector3d(0, 0, 1); SetCPlane(ctx, vp, cp, n); Finish(); return; }
    if (n == "Left") { cp = ConstructionPlane{}; cp.x_axis = Vector3d(0, -1, 0); cp.y_axis = Vector3d(0, 0, 1); SetCPlane(ctx, vp, cp, n); Finish(); return; }
    if (n == "View") { cp.origin = vp->GetCamera().State().target; cp.x_axis = vp->GetCamera().Right(); cp.y_axis = vp->GetCamera().Up(); SetCPlane(ctx, vp, cp, "View"); Finish(); return; }
    if (n == "Elevation") {
      if (v.empty()) { mode_ = Mode::Elevation; WantNumber("Elevation above the current CPlane"); return; }
      ApplyElevation(ctx, std::strtod(v.c_str(), nullptr)); Finish(); return;
    }
    if (n == "Rotate") {
      if (v.empty()) { mode_ = Mode::Rotate; WantNumber("Rotation angle about the CPlane normal (degrees)"); return; }
      ApplyRotate(ctx, std::strtod(v.c_str(), nullptr)); Finish(); return;
    }
    if (n == "Next" || n == "Previous") { ctx.Engine().Execute(n == "Next" ? "CPlaneNext" : "CPlanePrevious"); Finish(); return; }
    if (n == "Object" || n == "Surface" || n == "Curve") {
      mode_ = n == "Object" ? Mode::Object : n == "Surface" ? Mode::Surface : Mode::Curve;
      ctx.Doc().SelectNone();
      WantObjects(n == "Object" ? "Select a planar object" : n == "Surface" ? "Select a surface" : "Select a curve");
      return;
    }
    if (n == "3Point") { mode_ = Mode::ThreePoint; pts_.clear(); WantPoint("CPlane origin"); return; }
  }
  void ApplyElevation(CommandContext& ctx, double e) {
    Viewport* vp = ctx.ActiveViewport();
    ConstructionPlane cp = vp->CPlane();
    cp.origin = cp.origin + cp.Normal() * e;
    SetCPlane(ctx, vp, cp, "elevation " + FormatNumber(e));
  }
  void ApplyRotate(CommandContext& ctx, double deg) {
    Viewport* vp = ctx.ActiveViewport();
    ConstructionPlane cp = vp->CPlane();
    ON_Xform r;
    r.Rotation(deg * ON_PI / 180.0, cp.Normal(), cp.origin);
    cp.x_axis = r * cp.x_axis; cp.y_axis = r * cp.y_axis;
    cp.x_axis.Unitize(); cp.y_axis.Unitize();
    SetCPlane(ctx, vp, cp, "rotated " + FormatNumber(deg) + " degrees");
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (mode_ == Mode::Elevation) ApplyElevation(ctx, v);
    else if (mode_ == Mode::Rotate) ApplyRotate(ctx, v);
    Finish();
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    Viewport* vp = ctx.ActiveViewport();
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (mode_ == Mode::Surface && o->kind != ObjectKind::Surface && o->kind != ObjectKind::Brep) continue;
      if (mode_ == Mode::Curve && o->kind != ObjectKind::Curve) continue;
      ON_Plane pl;
      if (!PlaneOfObject(*o, pl, -vp->GetCamera().Forward())) continue;
      if (mode_ == Mode::Curve && o->curve) {
        // Perpendicular to the curve at its start (Rhino's CPlane Curve).
        const kernel::Interval d = o->curve->Domain();
        pl = ON_Plane(o->curve->PointAt(d.min), o->curve->TangentAt(d.min));
      }
      SetCPlane(ctx, vp, CPlaneFromPlane(pl), "aligned to object " + std::to_string(id));
      Finish();
      return;
    }
    ctx.Warn("No suitable object selected");
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) { WantPoint("X axis direction. Press Enter to keep the current orientation"); return; }
    if (pts_.size() == 2) { WantPoint("Y axis direction. Press Enter for the CPlane normal"); return; }
    ConstructionPlane cp = vp->CPlane();
    Vector3d x = pts_[1] - pts_[0], y = pts_[2] - pts_[0];
    if (x.Unitize() && y.Unitize()) { Vector3d z = ON_CrossProduct(x, y); if (z.Unitize()) { cp.origin = pts_[0]; cp.x_axis = x; cp.y_axis = ON_CrossProduct(z, x); } }
    SetCPlane(ctx, vp, cp, "3 points");
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    Viewport* vp = ctx.ActiveViewport();
    if (vp && !pts_.empty()) {
      ConstructionPlane cp = vp->CPlane();
      cp.origin = pts_[0];
      if (pts_.size() >= 2) { Vector3d x = pts_[1] - pts_[0]; if (x.Unitize()) { Vector3d z = cp.Normal(); cp.x_axis = x; cp.y_axis = ON_CrossProduct(z, x); cp.y_axis.Unitize(); } }
      SetCPlane(ctx, vp, cp, "origin");
    }
    Finish();
  }
  enum class Mode { Points, Elevation, Rotate, Object, Surface, Curve, ThreePoint };
  Mode mode_ = Mode::Points;
  std::vector<Point3d> pts_;
};

void CPlaneStep(CommandContext& ctx, bool next) {
  Viewport* vp = ctx.ActiveViewport();
  if (!vp) return;
  CPlaneHistory& h = ctx.App().viewtools.cplane_history[vp->Name()];
  std::vector<ConstructionPlane>& from = next ? h.redo : h.undo;
  std::vector<ConstructionPlane>& to = next ? h.undo : h.redo;
  if (from.empty()) { ctx.Print(std::string(next ? "CPlaneNext" : "CPlanePrevious") + ": no " + (next ? "next" : "previous") + " CPlane"); return; }
  to.push_back(vp->CPlane());
  vp->CPlane() = from.back();
  from.pop_back();
  h.last = vp->CPlane();
  ctx.Print(std::string(next ? "CPlaneNext" : "CPlanePrevious") + ": " + Describe(vp->CPlane()));
}

class NamedCPlaneCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    std::map<std::string, std::string> opts;
    std::vector<std::string> pos = TakeOptions(ctx, opts);
    if (pos.empty()) { ctx.App().Panels().named_cplanes = true; Finish(); return; }
    const std::string action = ToLower(pos[0]);
    std::string name = StringOr(opts, "name", pos.size() > 1 ? pos[1] : "");
    for (size_t i = 2; i < pos.size(); ++i) name += " " + pos[i];
    Viewport* vp = ctx.ActiveViewport();
    if (action == "save") {
      if (!vp) { Finish(); return; }
      if (name.empty()) name = "CPlane " + std::to_string(ctx.Doc().NamedCPlanes().size() + 1);
      NamedCPlane n;
      n.name = name; n.origin = vp->CPlane().origin; n.x_axis = vp->CPlane().x_axis; n.y_axis = vp->CPlane().y_axis;
      if (NamedCPlane* e = ctx.Doc().FindNamedCPlane(name)) *e = n; else ctx.Doc().NamedCPlanes().push_back(n);
      ctx.Doc().Touch();
      ctx.Print("NamedCPlane: saved " + name + " (" + Describe(vp->CPlane()) + ")");
    } else if (action == "restore") {
      NamedCPlane* n = ctx.Doc().FindNamedCPlane(name);
      if (!n) { ctx.Warn("No named CPlane " + name); Finish(); return; }
      if (vp) { vp->CPlane().origin = n->origin; vp->CPlane().x_axis = n->x_axis; vp->CPlane().y_axis = n->y_axis; }
      ctx.Print("NamedCPlane: restored " + name + " in " + (vp ? vp->Name() : std::string("?")) + " (" + Describe(vp->CPlane()) + ")");
    } else if (action == "delete") {
      auto& v = ctx.Doc().NamedCPlanes();
      const size_t before = v.size();
      v.erase(std::remove_if(v.begin(), v.end(), [&](const NamedCPlane& c) { return c.name == name; }), v.end());
      ctx.Print("NamedCPlane: " + std::to_string(before - v.size()) + " deleted");
      ctx.Doc().Touch();
    } else if (action == "list") {
      for (const NamedCPlane& c : ctx.Doc().NamedCPlanes()) ctx.Print("  " + c.name + ": origin " + FormatPoint(c.origin));
      ctx.Print("NamedCPlane: " + std::to_string(ctx.Doc().NamedCPlanes().size()) + " named CPlane(s)");
    } else {
      ctx.Warn("NamedCPlane: use Save <name>, Restore <name>, Delete <name> or List");
    }
    Finish();
  }
};

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

void ApplyFrame(CommandContext& ctx, int index, const char* label) {
  Animation& a = ctx.Doc().GetAnimation();
  if (a.frames.empty()) { ctx.Warn(std::string(label) + ": no animation (SetTurntableAnimation / SetPathAnimation first)"); return; }
  index = std::clamp(index, 0, static_cast<int>(a.frames.size()) - 1);
  Viewport* vp = ctx.App().FindViewport(a.viewport);
  if (!vp) vp = ctx.ActiveViewport();
  if (!vp) return;
  vp->GetCamera().SetState(a.frames[static_cast<size_t>(index)]);
  AnimationPlayback& pb = ctx.App().viewtools.playback;
  pb.current = index + 1;
  pb.applied = index;
  ctx.Print(std::string(label) + ": frame " + std::to_string(index + 1) + " of " + std::to_string(a.frames.size()));
}

void StoreAnimation(CommandContext& ctx, const std::string& kind, std::vector<CameraState> frames) {
  Animation& a = ctx.Doc().GetAnimation();
  a.kind = kind;
  a.frames = std::move(frames);
  a.viewport = ctx.ActiveViewport() ? ctx.ActiveViewport()->Name() : "";
  ctx.App().viewtools.playback = AnimationPlayback{};
  ctx.Doc().Touch();
}

void SetTurntable(CommandContext& ctx) {
  std::map<std::string, std::string> opts;
  const std::vector<std::string> pos = TakeOptions(ctx, opts);
  const int frames = std::max(2, static_cast<int>(NumberOr(opts, "frames", pos.empty() ? 36 : std::atof(pos[0].c_str()))));
  const double degrees = NumberOr(opts, "degrees", pos.size() > 1 ? std::atof(pos[1].c_str()) : 360.0);
  const bool cw = ToLower(StringOr(opts, "direction", "counterclockwise")) == "clockwise";
  Viewport* vp = ctx.ActiveViewport();
  if (!vp) return;
  const CameraState base = vp->GetCamera().State();
  std::vector<CameraState> out;
  for (int i = 0; i < frames; ++i) {
    const double ang = (cw ? -1 : 1) * degrees * ON_PI / 180.0 * i / frames;
    ON_Xform r;
    r.Rotation(ang, Vector3d(0, 0, 1), base.target);
    CameraState c = base;
    c.eye = r * base.eye;
    c.up = r * base.up;
    out.push_back(c);
  }
  StoreAnimation(ctx, "Turntable", out);
  ctx.Print("SetTurntableAnimation: " + std::to_string(frames) + " frames over " + FormatNumber(degrees) + " degrees in " + vp->Name());
}

std::vector<Point3d> SampleCurve(const kernel::NurbsCurve& c, int n) {
  std::vector<Point3d> pts;
  const double len = c.Length();
  for (int i = 0; i < n; ++i) {
    const double t = n > 1 ? c.ParameterAtArcLength(len * i / (n - 1)) : c.Domain().min;
    pts.push_back(c.PointAt(t));
  }
  return pts;
}

class PathAnimationCommand : public Command {
 public:
  explicit PathAnimationCommand(bool flythrough) : fly_(flythrough) {}
  void Begin(CommandContext& ctx) override {
    std::map<std::string, std::string> opts;
    TakeOptions(ctx, opts);
    frames_ = std::max(2, static_cast<int>(NumberOr(opts, "frames", 30)));
    WantObjects("Select the camera path curve");
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    const SceneObject* o = nullptr;
    for (ObjectId id : ids) { const SceneObject* c = ctx.Doc().Find(id); if (c && c->kind == ObjectKind::Curve && c->curve && c->id != camera_id_) { o = c; break; } }
    if (!o) { ctx.Warn("Select a curve"); Finish(); return; }
    ctx.Doc().SelectNone();
    if (camera_id_ == kNoObject) {
      camera_id_ = o->id;
      if (fly_) { WantObjects("Select the target path curve"); return; }
      WantPoint("Target point");
      return;
    }
    Build(ctx, SampleCurve(*o->curve, frames_));
  }
  void OnPoint(CommandContext& ctx, Point3d p) override { Build(ctx, std::vector<Point3d>(static_cast<size_t>(frames_), p)); }
  void Build(CommandContext& ctx, const std::vector<Point3d>& targets) {
    const SceneObject* cam = ctx.Doc().Find(camera_id_);
    Viewport* vp = ctx.ActiveViewport();
    if (!cam || !cam->curve || !vp) { Finish(); return; }
    const std::vector<Point3d> eyes = SampleCurve(*cam->curve, frames_);
    std::vector<CameraState> out;
    for (size_t i = 0; i < eyes.size() && i < targets.size(); ++i) {
      CameraState c = vp->GetCamera().State();
      c.perspective = true;
      c.eye = eyes[i];
      c.target = targets[i];
      Vector3d f = c.target - c.eye;
      if (f.Unitize() && std::fabs(ON_DotProduct(f, Vector3d(0, 0, 1))) > 0.999) c.up = Vector3d(0, 1, 0); else c.up = Vector3d(0, 0, 1);
      out.push_back(c);
    }
    StoreAnimation(ctx, fly_ ? "Flythrough" : "Path", out);
    ctx.Print(std::string(fly_ ? "SetFlythroughAnimation" : "SetPathAnimation") + ": " + std::to_string(out.size()) + " frames along curve " + std::to_string(camera_id_) +
              (fly_ ? " with a target curve" : " looking at " + FormatPoint(targets.front())));
    Finish();
  }
  bool fly_;
  int frames_ = 30;
  ObjectId camera_id_ = kNoObject;
};

void StartPlayback(CommandContext& ctx, bool record) {
  std::map<std::string, std::string> opts;
  const std::vector<std::string> pos = TakeOptions(ctx, opts);
  Animation& a = ctx.Doc().GetAnimation();
  if (a.frames.empty()) { ctx.Warn(std::string(record ? "RecordAnimation" : "PlayAnimation") + ": no animation (SetTurntableAnimation / SetPathAnimation first)"); return; }
  AnimationPlayback& pb = ctx.App().viewtools.playback;
  pb = AnimationPlayback{};
  pb.playing = true;
  if (record) {
    pb.record_dir = StringOr(opts, "folder", pos.empty() ? (fs::temp_directory_path() / "dino8_animation").string() : pos[0]);
    std::error_code ec;
    fs::create_directories(pb.record_dir, ec);
    pb.recording = true;
    ctx.Print("RecordAnimation: recording " + std::to_string(a.frames.size()) + " frames to " + pb.record_dir);
  } else {
    pb.loops = static_cast<int>(NumberOr(opts, "loops", 1));
    ctx.Print("PlayAnimation: playing " + std::to_string(a.frames.size()) + " " + a.kind + " frames");
  }
}

// ---------------------------------------------------------------------------
// Misc viewport commands
// ---------------------------------------------------------------------------

std::string FirstToken(CommandContext& ctx) {
  std::map<std::string, std::string> opts;
  const std::vector<std::string> pos = TakeOptions(ctx, opts);
  std::string s = StringOr(opts, "viewport", StringOr(opts, "name", pos.empty() ? "" : pos[0]));
  for (size_t i = 1; i < pos.size(); ++i) s += " " + pos[i];
  return s;
}

void SplitViewport(CommandContext& ctx, bool horizontal) {
  Viewport* vp = ctx.ActiveViewport();
  if (!vp || vp->IsPage()) { ctx.Warn("Split works on model viewports"); return; }
  const CameraState cam = vp->GetCamera().State();
  const DisplayMode mode = vp->Mode();
  const ConstructionPlane cp = vp->CPlane();
  Viewport* nv = ctx.App().AddViewport(vp->Name(), vp->StandardView());
  nv->GetCamera().SetState(cam);
  nv->SetMode(mode);
  nv->CPlane() = cp;
  ctx.Print(std::string(horizontal ? "SplitViewportHorizontal" : "SplitViewportVertical") + ": added " + nv->Name() + " (" + std::to_string(ctx.Viewports().size()) + " viewports)");
}

void WalkAbout(CommandContext& ctx) {
  std::map<std::string, std::string> opts;
  const std::vector<std::string> pos = TakeOptions(ctx, opts);
  Viewport* vp = ctx.ActiveViewport();
  if (!vp) return;
  std::string dir = ToLower(StringOr(opts, "direction", pos.empty() ? "" : pos[0]));
  if (dir.empty()) { ctx.Print("WalkAbout: WalkAbout <Forward|Back|Left|Right|Up|Down|TurnLeft|TurnRight> [distance]"); return; }
  const double step = NumberOr(opts, "distance", pos.size() > 1 ? std::atof(pos[1].c_str()) : vp->GetCamera().Distance() * 0.1);
  Camera& cam = vp->GetCamera();
  CameraState& s = cam.State();
  Vector3d f = cam.Forward(), r = cam.Right();
  Vector3d flat(f.x, f.y, 0);
  if (!flat.Unitize()) flat = f;
  Vector3d d(0, 0, 0);
  if (dir == "forward" || dir == "f" || dir == "w") d = flat * step;
  else if (dir == "back" || dir == "backward" || dir == "b" || dir == "s") d = -flat * step;
  else if (dir == "left" || dir == "a") d = -r * step;
  else if (dir == "right") d = r * step;
  else if (dir == "up") d = Vector3d(0, 0, step);
  else if (dir == "down") d = Vector3d(0, 0, -step);
  else if (dir == "turnleft" || dir == "turnright") {
    ON_Xform rot;
    rot.Rotation((dir == "turnleft" ? 1 : -1) * ON_PI / 12, Vector3d(0, 0, 1), s.eye);
    s.target = rot * s.target;
    ctx.Print("WalkAbout: turned 15 degrees " + dir.substr(4));
    return;
  } else { ctx.Warn("WalkAbout: unknown direction " + dir); return; }
  s.eye = s.eye + d;
  s.target = s.target + d;
  ctx.Print("WalkAbout: moved " + dir + " " + FormatNumber(step));
}

double UnitsPerMillimetre(const std::string& unit) {
  if (unit == "Centimeters") return 0.1;
  if (unit == "Meters") return 0.001;
  if (unit == "Inches") return 1.0 / 25.4;
  if (unit == "Feet") return 1.0 / 304.8;
  return 1.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Per-frame hook (animation, CPlane history, AutoAlignCPlane, MPlane)
// ---------------------------------------------------------------------------

void ViewToolsFrame(Application& app) {
  ViewToolsState& st = app.viewtools;
  Document& doc = app.Doc();
  CommandEngine& engine = app.Engine();

  // Animation: one frame per application frame; recording captures the
  // frame that was rendered last time before moving on.
  AnimationPlayback& pb = st.playback;
  if (pb.playing) {
    Animation& a = doc.GetAnimation();
    Viewport* vp = app.FindViewport(a.viewport);
    if (!vp) vp = app.ActiveViewport();
    if (a.frames.empty() || !vp) {
      pb.playing = pb.recording = false;
    } else {
      if (pb.recording && pb.applied >= 0) {
        char name[64];
        std::snprintf(name, sizeof(name), "/frame_%04d.bmp", pb.applied + 1);
        std::string err;
        if (vp->CaptureToFile(pb.record_dir + name, err)) ++pb.recorded;
        else engine.Print("! RecordAnimation: " + err);
        pb.applied = -1;
      }
      if (pb.current >= static_cast<int>(a.frames.size())) {
        if (pb.recording) {
          engine.Print("RecordAnimation: wrote " + std::to_string(pb.recorded) + " frames to " + pb.record_dir);
          pb.recording = false;
          pb.playing = false;
        } else if (pb.loops > 1) {
          --pb.loops;
          pb.current = 0;
        } else {
          engine.Print("PlayAnimation: finished " + std::to_string(a.frames.size()) + " frames");
          pb.playing = false;
        }
      }
      if (pb.playing) {
        vp->GetCamera().SetState(a.frames[static_cast<size_t>(pb.current)]);
        pb.applied = pb.current;
        ++pb.current;
      }
    }
  }

  // MPlane: the CPlane follows its object.
  if (st.mplane_object != kNoObject) {
    const SceneObject* o = doc.Find(st.mplane_object);
    Viewport* vp = app.FindViewport(st.mplane_viewport);
    if (!o || !vp) {
      st.mplane_object = kNoObject;
    } else {
      const kernel::BoundingBox bb = o->BoundingBox();
      const Point3d c((bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2, (bb.min.z + bb.max.z) / 2);
      vp->CPlane().origin = c + st.mplane_offset;
      st.cplane_history[vp->Name()].last = vp->CPlane();
    }
  }

  // AutoAlignCPlane: a new selection aligns the active CPlane to it.
  if (st.auto_align_cplane) {
    const std::vector<ObjectId> sel = doc.SelectedIds();
    if (sel != st.last_selection) {
      st.last_selection = sel;
      Viewport* vp = app.ActiveViewport();
      if (vp && !sel.empty() && !engine.IsRunning()) {
        for (ObjectId id : sel) {
          const SceneObject* o = doc.Find(id);
          ON_Plane pl;
          if (o && PlaneOfObject(*o, pl, -vp->GetCamera().Forward())) {
            vp->CPlane() = CPlaneFromPlane(pl);
            engine.Print("AutoAlignCPlane: CPlane aligned to object " + std::to_string(id));
            break;
          }
        }
      }
    }
  }

  // CPlane history for CPlanePrevious / CPlaneNext.
  for (auto& vp : app.Viewports()) {
    CPlaneHistory& h = st.cplane_history[vp->Name()];
    const ConstructionPlane& cp = vp->CPlane();
    if (!h.last) { h.last = cp; continue; }
    if (SameCPlane(*h.last, cp)) continue;
    h.undo.push_back(*h.last);
    if (h.undo.size() > 50) h.undo.erase(h.undo.begin());
    h.redo.clear();
    h.last = cp;
  }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void RegisterViewToolsCommands(CommandEngine& e) {
  // ---- clipping planes ----
  Reg(e, "ClippingPlane", Make<ClippingPlaneCommand>());
  Reg(e, "EnableClippingPlane", Immediate([](CommandContext& ctx) { SetPlanesEnabled(ctx, true); }));
  Reg(e, "DisableClippingPlane", Immediate([](CommandContext& ctx) { SetPlanesEnabled(ctx, false); }));
  Reg(e, "ClippingPlaneProperties", Immediate([](CommandContext& ctx) { ctx.App().Panels().clipping_planes = true; }));
  Reg(e, "SelClippingPlane", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        const std::vector<std::string> names = TakeOptions(ctx, opts);
        int n = 0;
        for (ClippingPlane& cp : ctx.Doc().ClippingPlanes()) {
          bool take = names.empty();
          for (const std::string& nm : names) if (ToLower(nm) == ToLower(cp.name) || nm == std::to_string(cp.id)) take = true;
          if (take) { cp.selected = true; ++n; }
        }
        ctx.Print("SelClippingPlane: " + std::to_string(n) + " clipping plane(s) selected");
      }));
  Reg(e, "SelClippingPlaneInViewport", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        int n = 0;
        for (ClippingPlane& cp : ctx.Doc().ClippingPlanes()) if (vp && cp.ClipsViewport(vp->Name())) { cp.selected = true; ++n; }
        ctx.Print("SelClippingPlaneInViewport: " + std::to_string(n) + " clipping plane(s) clip " + (vp ? vp->Name() : std::string("?")));
      }));
  Reg(e, "ClippingSections", Immediate([](CommandContext& ctx) { ClippingSections(ctx, "ClippingSections"); }));
  Reg(e, "ExtractClippingSections", Immediate([](CommandContext& ctx) { ClippingSections(ctx, "ExtractClippingSections"); }));
  Reg(e, "ExtractClippingSlices", Immediate([](CommandContext& ctx) { ClippingSections(ctx, "ExtractClippingSlices"); }), CommandStatus::Partial, "Extracts the section curves; planar slice surfaces are planned.");
  Reg(e, "SaveClippingSectionCPlanes", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        std::vector<ClippingPlane*> planes = TargetPlanes(ctx, TakeOptions(ctx, opts));
        for (ClippingPlane* cp : planes) {
          NamedCPlane n; n.name = cp->name; n.origin = cp->origin; n.x_axis = cp->x_axis; n.y_axis = cp->y_axis;
          if (NamedCPlane* ex = ctx.Doc().FindNamedCPlane(n.name)) *ex = n; else ctx.Doc().NamedCPlanes().push_back(n);
        }
        ctx.Doc().Touch();
        ctx.Print("SaveClippingSectionCPlanes: " + std::to_string(planes.size()) + " named CPlane(s) saved");
      }));
  Reg(e, "SaveClippingSectionViews", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        std::vector<ClippingPlane*> planes = TargetPlanes(ctx, TakeOptions(ctx, opts));
        Viewport* vp = ctx.ActiveViewport();
        for (ClippingPlane* cp : planes) {
          NamedView nv; nv.name = cp->name; nv.camera = SectionCamera(*cp, vp ? vp->GetCamera().State() : CameraState{});
          bool replaced = false;
          for (NamedView& ex : ctx.Doc().NamedViews()) if (ex.name == nv.name) { ex = nv; replaced = true; }
          if (!replaced) ctx.Doc().NamedViews().push_back(nv);
        }
        ctx.Doc().Touch();
        ctx.Print("SaveClippingSectionViews: " + std::to_string(planes.size()) + " named view(s) saved");
      }));
  Reg(e, "ViewClippingSections", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        std::vector<ClippingPlane*> planes = TargetPlanes(ctx, TakeOptions(ctx, opts));
        Viewport* vp = ctx.ActiveViewport();
        if (planes.empty() || !vp) { ctx.Warn("No clipping planes"); return; }
        vp->GetCamera().SetState(SectionCamera(*planes.front(), vp->GetCamera().State()));
        ctx.Print("ViewClippingSections: " + vp->Name() + " looks at " + planes.front()->name);
      }));
  Reg(e, "ExportClippingSectionInfo", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        const std::vector<std::string> pos = TakeOptions(ctx, opts);
        const std::string path = StringOr(opts, "file", pos.empty() ? (fs::temp_directory_path() / "clipping_sections.txt").string() : pos[0]);
        std::ofstream out(path);
        if (!out) { ctx.Warn("Cannot write " + path); return; }
        out << "# Dino 8 clipping planes\n";
        for (const ClippingPlane& cp : ctx.Doc().ClippingPlanes()) {
          out << cp.name << "\torigin " << FormatPoint(cp.origin) << "\tnormal " << FormatPoint(Point3d(cp.Normal())) << "\tsize " << cp.width << " x " << cp.height
              << "\t" << (cp.enabled ? "enabled" : "disabled") << "\tviewports " << (cp.viewports.empty() ? std::string("all") : cp.viewports.front()) << "\n";
        }
        ctx.Print("ExportClippingSectionInfo: wrote " + std::to_string(ctx.Doc().ClippingPlanes().size()) + " plane(s) to " + path);
      }));
  Reg(e, "ClearClippingSections", Immediate([](CommandContext& ctx) {
        std::vector<ObjectId> ids;
        for (const SceneObject& o : ctx.Doc().Objects()) if (o.user_text.count("ClippingSection")) ids.push_back(o.id);
        if (!ids.empty()) { ctx.Doc().BeginChange("ClearClippingSections"); for (ObjectId id : ids) ctx.Doc().Remove(id); }
        ctx.Print("ClearClippingSections: removed " + std::to_string(ids.size()) + " section curve(s)");
      }));
  for (const char* n : {"ClippingDrawings", "EditClippingDrawings", "ExportClippingDrawings", "NestedClippingDrawing", "UpdateClippingDrawings"}) {
    Reg(e, n, Immediate([n](CommandContext& ctx) { ctx.Print(std::string(n) + ": clipping drawings are planned; use ClippingSections to extract section curves and Print for output."); }), CommandStatus::Partial, "Use ClippingSections + Print.");
  }
  Reg(e, "ShowZBuffer", Immediate([](CommandContext& ctx) {
        bool& z = ctx.App().viewtools.show_zbuffer;
        z = !z;
        ctx.Print(std::string("ShowZBuffer: ") + (z ? "on (depth view is planned; the flag is recorded)" : "off"));
      }), CommandStatus::Partial, "Toggles the flag only.");

  // ---- layouts / details ----
  Reg(e, "Layout", Make<LayoutCommand>());
  Reg(e, "Layouts", Immediate([](CommandContext& ctx) {
        ctx.App().Panels().layouts = true;
        const std::string name = FirstToken(ctx);
        if (!name.empty()) { if (ctx.App().SetActiveLayoutByName(name)) ctx.Print("Layouts: switched to " + name); else ctx.Warn("No layout named " + name); }
        for (const Layout& L : ctx.Doc().Layouts()) ctx.Print("  " + L.name + ": " + FormatNumber(L.width_mm) + " x " + FormatNumber(L.height_mm) + " mm, " + std::to_string(L.details.size()) + " detail(s)");
        ctx.Print("Layouts: " + std::to_string(ctx.Doc().Layouts().size()) + " layout(s); active: " + (ctx.App().ActiveLayout() ? ctx.App().ActiveLayout()->name : std::string("Model")));
      }));
  Reg(e, "LayoutProperties", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        TakeOptions(ctx, opts);
        Layout* L = ctx.App().ActiveLayout();
        if (!L) { ctx.Warn("No active layout"); ctx.App().Panels().layouts = true; return; }
        if (opts.count("name")) { L->name = UniqueLayoutName(ctx.Doc(), opts["name"]); }
        if (opts.count("width")) L->width_mm = std::max(10.0, NumberOr(opts, "width", L->width_mm));
        if (opts.count("height")) L->height_mm = std::max(10.0, NumberOr(opts, "height", L->height_mm));
        if (!opts.empty()) ctx.Doc().Touch();
        ctx.App().Panels().layouts = true;
        ctx.Print("LayoutProperties: " + L->name + " " + FormatNumber(L->width_mm) + " x " + FormatNumber(L->height_mm) + " mm, " + std::to_string(L->details.size()) + " detail(s)");
      }));
  Reg(e, "CopyLayout", Immediate([](CommandContext& ctx) {
        Layout* L = ctx.App().ActiveLayout();
        if (!L) { ctx.Warn("No active layout"); return; }
        Layout copy = *L;
        copy.name = UniqueLayoutName(ctx.Doc(), StringOr(std::map<std::string, std::string>{}, "name", L->name + " Copy"));
        for (LayoutDetail& d : copy.details) { d.selected = false; }
        ctx.Doc().BeginChange("CopyLayout");
        ctx.Doc().Layouts().push_back(copy);
        ctx.Doc().Touch();
        ctx.App().SetActiveLayout(static_cast<int>(ctx.Doc().Layouts().size()) - 1);
        ctx.Print("CopyLayout: created " + copy.name + " with " + std::to_string(copy.details.size()) + " detail(s)");
      }));
  Reg(e, "ImportLayout", Immediate([](CommandContext& ctx) {
        const std::string path = FirstToken(ctx);
        if (path.empty()) { ctx.Warn("ImportLayout: ImportLayout <file.3dm>"); return; }
        Document other;
        std::string err;
        if (!Load3dm(other, path, err)) { ctx.Warn(err); return; }
        ctx.Doc().BeginChange("ImportLayout");
        int n = 0;
        for (const Layout& L : other.Layouts()) { Layout c = L; c.name = UniqueLayoutName(ctx.Doc(), L.name); ctx.Doc().Layouts().push_back(c); ++n; }
        ctx.Doc().Touch();
        ctx.Print("ImportLayout: imported " + std::to_string(n) + " layout(s) from " + path);
      }), CommandStatus::Partial, "Imports the page and detail cameras; per-detail hidden objects are not mapped.");
  Reg(e, "Detail", Make<DetailCommand>());
  Reg(e, "CopyDetailToViewport", Immediate([](CommandContext& ctx) { CopyDetailCamera(ctx, true); }));
  Reg(e, "CopyViewportToDetail", Immediate([](CommandContext& ctx) { CopyDetailCamera(ctx, false); }));
  Reg(e, "HideInDetail", OnSelection("Select objects to hide in the detail", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { HideShowInDetail(ctx, ids, 0); }));
  Reg(e, "ShowInDetail", Immediate([](CommandContext& ctx) { HideShowInDetail(ctx, {}, 1); }));
  Reg(e, "ShowSelectedInDetail", OnSelection("Select objects to show in the detail", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { HideShowInDetail(ctx, ids, 2); }));
  Reg(e, "HideLayersInDetail", Immediate([](CommandContext& ctx) { HideShowLayersInDetail(ctx, true); }));
  Reg(e, "ShowLayersInDetail", Immediate([](CommandContext& ctx) { HideShowLayersInDetail(ctx, false); }));
  Reg(e, "SelDetail", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        const std::vector<std::string> names = TakeOptions(ctx, opts);
        Layout* L = ctx.App().ActiveLayout();
        if (!L) { ctx.Warn("No active layout"); return; }
        int n = 0;
        for (LayoutDetail& d : L->details) {
          bool take = names.empty();
          for (const std::string& nm : names) if (ToLower(nm) == ToLower(d.name)) take = true;
          if (take) { d.selected = true; ++n; }
        }
        ctx.Print("SelDetail: " + std::to_string(n) + " detail(s) selected");
      }));
  Reg(e, "ViewportTabs", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        TakeOptions(ctx, opts);
        bool& t = ctx.App().show_viewport_tabs;
        t = opts.count("state") ? IsYes(opts["state"]) : !t;
        ctx.Print(std::string("ViewportTabs: ") + (t ? "shown" : "hidden"));
      }));
  Reg(e, "PrintDisplay", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        TakeOptions(ctx, opts);
        bool& p = ctx.App().viewtools.print_display;
        p = opts.count("state") ? IsYes(opts["state"]) : !p;
        ctx.Print(std::string("PrintDisplay: ") + (p ? "on (print line widths previewed)" : "off"));
      }), CommandStatus::Partial, "Previews line widths; print colours are planned.");

  // ---- CPlanes ----
  Reg(e, "CPlane", Make<CPlaneCommand>());
  Reg(e, "CPlaneNext", Immediate([](CommandContext& ctx) { CPlaneStep(ctx, true); }));
  Reg(e, "CPlanePrevious", Immediate([](CommandContext& ctx) { CPlaneStep(ctx, false); }));
  Reg(e, "NamedCPlane", Make<NamedCPlaneCommand>());
  auto copy_all = [](CommandContext& ctx, const char* label) {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) return;
    int n = 0;
    for (auto& o : ctx.Viewports()) if (o.get() != vp) { o->CPlane() = vp->CPlane(); ++n; }
    ctx.Print(std::string(label) + ": CPlane of " + vp->Name() + " copied to " + std::to_string(n) + " viewport(s)");
  };
  Reg(e, "CopyCPlaneToAll", Immediate([copy_all](CommandContext& ctx) { copy_all(ctx, "CopyCPlaneToAll"); }));
  Reg(e, "SynchronizeCPlanes", Immediate([copy_all](CommandContext& ctx) { copy_all(ctx, "SynchronizeCPlanes"); }));
  Reg(e, "CopyCPlaneSettingsToAll", Immediate([copy_all](CommandContext& ctx) {
        copy_all(ctx, "CopyCPlaneSettingsToAll");
        ctx.Print("CopyCPlaneSettingsToAll: grid spacing " + FormatNumber(ctx.Settings().grid_spacing) + ", major every " + std::to_string(ctx.Settings().grid_major_every) + " (document-wide)");
      }));
  Reg(e, "RemapCPlane", OnSelection("Select objects to remap to another CPlane", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        const std::string target = FirstToken(ctx);
        Viewport* from = ctx.ActiveViewport();
        Viewport* to = target.empty() ? nullptr : ctx.App().FindViewport(target);
        if (!from) return;
        if (!to) { ctx.Warn("RemapCPlane: RemapCPlane <viewport> - remaps from " + from->Name() + "'s CPlane to that viewport's CPlane"); return; }
        ON_Xform xf;
        xf.Rotation(PlaneOf(from->CPlane()), PlaneOf(to->CPlane()));
        ctx.Doc().BeginChange("RemapCPlane");
        for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) o->Transform(xf);
        ctx.Print("RemapCPlane: " + std::to_string(ids.size()) + " object(s) remapped from " + from->Name() + " to " + to->Name());
      }));
  Reg(e, "AutoAlignCPlane", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        TakeOptions(ctx, opts);
        ViewToolsState& st = ctx.App().viewtools;
        st.auto_align_cplane = opts.count("state") ? IsYes(opts["state"]) : !st.auto_align_cplane;
        st.last_selection = ctx.Doc().SelectedIds();
        ctx.Print(std::string("AutoAlignCPlane: ") + (st.auto_align_cplane ? "on - selecting a planar face or curve aligns the CPlane" : "off"));
      }));
  Reg(e, "MPlane", OnSelection("Select the object the CPlane should follow", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Viewport* vp = ctx.ActiveViewport();
        ViewToolsState& st = ctx.App().viewtools;
        if (!vp || ids.empty()) { st.mplane_object = kNoObject; ctx.Print("MPlane: detached"); return; }
        const SceneObject* o = ctx.Doc().Find(ids.front());
        if (!o) return;
        const kernel::BoundingBox bb = o->BoundingBox();
        const Point3d c((bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2, (bb.min.z + bb.max.z) / 2);
        st.mplane_object = o->id;
        st.mplane_viewport = vp->Name();
        st.mplane_offset = Vector3d(0, 0, 0);
        vp->CPlane().origin = c;
        ctx.Print("MPlane: CPlane of " + vp->Name() + " follows object " + std::to_string(o->id) + " (origin " + FormatPoint(c) + ")");
      }), CommandStatus::Partial, "Follows the object's bounding-box centre; orientation tracking is planned.");
  Reg(e, "OrientCameraToSrf", OnSelection("Select a surface to look at", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          ON_Plane pl;
          if (!o || (o->kind != ObjectKind::Surface && o->kind != ObjectKind::Brep) || !PlaneOfObject(*o, pl, -vp->GetCamera().Forward())) continue;
          CameraState& c = vp->GetCamera().State();
          const double dist = vp->GetCamera().Distance();
          c.target = pl.origin;
          c.eye = pl.origin + pl.zaxis * dist;
          c.up = std::fabs(ON_DotProduct(pl.zaxis, Vector3d(0, 0, 1))) > 0.99 ? Vector3d(0, 1, 0) : Vector3d(0, 0, 1);
          ctx.Print("OrientCameraToSrf: camera looks along the normal of object " + std::to_string(id) + " at " + FormatPoint(pl.origin));
          return;
        }
        ctx.Warn("Select a surface or polysurface");
      }), CommandStatus::Partial, "Uses the surface centre; picking a point on the surface is planned.");
  Reg(e, "PerspectiveMatch", Immediate([](CommandContext& ctx) { ctx.Print("PerspectiveMatch: matching a camera to a background image is planned."); }), CommandStatus::Partial);

  // ---- animation ----
  Reg(e, "SetTurntableAnimation", Immediate(SetTurntable));
  Reg(e, "SetPathAnimation", Make<PathAnimationCommand>(false));
  Reg(e, "SetFlythroughAnimation", Make<PathAnimationCommand>(true));
  Reg(e, "SetOneDaySunAnimation", Immediate([](CommandContext& ctx) { ctx.Print("SetOneDaySunAnimation: sun animation is planned (no Sun in this build); use SetTurntableAnimation."); }), CommandStatus::Partial);
  Reg(e, "SetSeasonalSunAnimation", Immediate([](CommandContext& ctx) { ctx.Print("SetSeasonalSunAnimation: sun animation is planned (no Sun in this build); use SetTurntableAnimation."); }), CommandStatus::Partial);
  Reg(e, "PlayAnimation", Immediate([](CommandContext& ctx) { StartPlayback(ctx, false); }));
  Reg(e, "RecordAnimation", Immediate([](CommandContext& ctx) { StartPlayback(ctx, true); }));
  Reg(e, "ViewFirstFrame", Immediate([](CommandContext& ctx) { ApplyFrame(ctx, 0, "ViewFirstFrame"); }));
  Reg(e, "ViewLastFrame", Immediate([](CommandContext& ctx) { ApplyFrame(ctx, static_cast<int>(ctx.Doc().GetAnimation().frames.size()) - 1, "ViewLastFrame"); }));
  Reg(e, "ViewNextFrame", Immediate([](CommandContext& ctx) { ApplyFrame(ctx, ctx.App().viewtools.playback.applied + 1, "ViewNextFrame"); }));
  Reg(e, "ViewPreviousFrame", Immediate([](CommandContext& ctx) { ApplyFrame(ctx, std::max(0, ctx.App().viewtools.playback.applied - 1), "ViewPreviousFrame"); }));
  Reg(e, "ViewFrameNumber", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        const std::vector<std::string> pos = TakeOptions(ctx, opts);
        const int n = static_cast<int>(NumberOr(opts, "frame", pos.empty() ? 1 : std::atof(pos[0].c_str())));
        ApplyFrame(ctx, n - 1, "ViewFrameNumber");
      }));

  // ---- viewport list ----
  Reg(e, "SplitViewportHorizontal", Immediate([](CommandContext& ctx) { SplitViewport(ctx, true); }));
  Reg(e, "SplitViewportVertical", Immediate([](CommandContext& ctx) { SplitViewport(ctx, false); }));
  Reg(e, "NewFloatingViewport", Immediate([](CommandContext& ctx) {
        std::string name = FirstToken(ctx);
        if (name.empty()) name = "Floating";
        Viewport* nv = ctx.App().AddViewport(name, "Perspective", true);
        ctx.Print("NewFloatingViewport: added " + nv->Name());
      }));
  Reg(e, "CloseViewport", Immediate([](CommandContext& ctx) {
        std::string name = FirstToken(ctx);
        if (name.empty() && ctx.ActiveViewport()) name = ctx.ActiveViewport()->Name();
        if (ctx.App().RemoveViewport(name)) ctx.Print("CloseViewport: closed " + name + " (" + std::to_string(ctx.Viewports().size()) + " left)");
        else ctx.Warn("CloseViewport: cannot close " + name + " (not a model viewport, or it is the last one)");
      }));
  Reg(e, "SetMaximizedViewport", Immediate([](CommandContext& ctx) {
        std::string name = FirstToken(ctx);
        Viewport* vp = name.empty() ? ctx.ActiveViewport() : ctx.App().FindViewport(name);
        if (!vp) { ctx.Warn("No viewport " + name); return; }
        for (auto& o : ctx.Viewports()) { o->SetMaximized(o.get() == vp); o->SetActive(o.get() == vp); }
        ctx.Print("SetMaximizedViewport: " + vp->Name());
      }));
  Reg(e, "BringViewportToTop", Immediate([](CommandContext& ctx) {
        std::string name = FirstToken(ctx);
        Viewport* vp = name.empty() ? ctx.ActiveViewport() : ctx.App().FindViewport(name);
        if (!vp) { ctx.Warn("No viewport " + name); return; }
        for (auto& o : ctx.Viewports()) o->SetActive(o.get() == vp);
        ctx.App().BringViewportToTop(vp->Name());
        ctx.Print("BringViewportToTop: " + vp->Name());
      }));
  Reg(e, "ToggleFloatingViewport", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp || vp->IsPage()) return;
        vp->SetFloating(!vp->Floating());
        ctx.App().RebuildLayout();
        ctx.Print("ToggleFloatingViewport: " + vp->Name() + (vp->Floating() ? " is now floating" : " is docked again"));
      }), CommandStatus::Partial, "Re-docks the other viewports in the default grid.");
  Reg(e, "ReadViewportsFromFile", Immediate([](CommandContext& ctx) {
        const std::string path = FirstToken(ctx);
        if (path.empty()) { ctx.Warn("ReadViewportsFromFile: ReadViewportsFromFile <file.3dm>"); return; }
        ONX_Model model;
        if (!model.Read(path.c_str())) { ctx.Warn("Cannot read " + path); return; }
        int n = 0;
        for (int i = 0; i < model.m_settings.m_views.Count(); ++i) {
          const ON_3dmView& v = model.m_settings.m_views[i];
          if (v.m_view_type != ON::model_view_type) continue;
          ON_String name(v.m_name);
          Viewport* vp = ctx.App().FindViewport(static_cast<const char*>(name));
          if (!vp) continue;
          CameraState c = vp->GetCamera().State();
          c.eye = v.m_vp.CameraLocation(); c.target = v.m_vp.TargetPoint(); c.up = v.m_vp.CameraUp(); c.perspective = v.m_vp.IsPerspectiveProjection();
          double l, r, b, t;
          if (!c.perspective && v.m_vp.GetFrustum(&l, &r, &b, &t) && t - b > 0) c.ortho_height = t - b;
          vp->GetCamera().SetState(c);
          ++n;
        }
        ctx.Print("ReadViewportsFromFile: applied " + std::to_string(n) + " viewport camera(s) from " + path);
      }), CommandStatus::Partial, "Applies cameras to same-named viewports; window positions are not restored.");
  Reg(e, "WalkAbout", Immediate(WalkAbout), CommandStatus::Partial, "Keyboard stepping by command (WalkAbout Forward 5); continuous walk mode is planned.");
  Reg(e, "Walkabout", Immediate(WalkAbout), CommandStatus::Partial, "Keyboard stepping by command (WalkAbout Forward 5); continuous walk mode is planned.");
  Reg(e, "SetZoomExtentsBorder", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        const std::vector<std::string> pos = TakeOptions(ctx, opts);
        double v = NumberOr(opts, "border", pos.empty() ? -1 : std::atof(pos[0].c_str()));
        if (v < 0) { ctx.Print("SetZoomExtentsBorder: current border factor " + FormatNumber(Camera::zoom_extents_border) + " (SetZoomExtentsBorder 1.1 or Border=10 for 10%)"); return; }
        if (v > 3) v = 1.0 + v / 100.0;  // percentage
        else if (v < 1) v = 1.0 + v;      // fraction
        Camera::zoom_extents_border = std::clamp(v, 1.0, 3.0);
        ctx.Print("SetZoomExtentsBorder: border factor " + FormatNumber(Camera::zoom_extents_border));
      }));
  Reg(e, "Zoom1To1Calibrate", Immediate([](CommandContext& ctx) {
        std::map<std::string, std::string> opts;
        const std::vector<std::string> pos = TakeOptions(ctx, opts);
        ViewToolsState& st = ctx.App().viewtools;
        const double v = NumberOr(opts, "pixelspermm", NumberOr(opts, "dpi", pos.empty() ? -1 : std::atof(pos[0].c_str())));
        if (v > 0) st.screen_pixels_per_mm = v > 20 ? v / 25.4 : v;  // > 20 reads as dpi
        ctx.Print("Zoom1To1Calibrate: " + FormatNumber(st.screen_pixels_per_mm) + " pixels per mm (" + FormatNumber(st.screen_pixels_per_mm * 25.4) + " dpi)");
      }), CommandStatus::Partial, "Takes the value from the command line (pixels per mm or dpi); the on-screen ruler is planned.");
  Reg(e, "Zoom1To1", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        CameraState& c = vp->GetCamera().State();
        c.perspective = false;
        c.ortho_height = vp->Height() / ctx.App().viewtools.screen_pixels_per_mm * UnitsPerMillimetre(ctx.Settings().unit_system);
        ctx.Print("Zoom1To1: " + vp->Name() + " shows " + FormatNumber(c.ortho_height) + " " + ctx.Settings().unit_system + " over " + std::to_string(vp->Height()) + " px");
      }), CommandStatus::Partial, "Uses the Zoom1To1Calibrate value (default 96 dpi).");
}

}  // namespace dino8::app
