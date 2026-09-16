// View, viewport, display-mode and CPlane commands.
#include "commands/cmd_common.h"

namespace dino8::app {

namespace {

CommandFactory SetView(const char* view) {
  return Immediate([view](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { vp->PushViewHistory(); vp->SetStandardView(view); vp->ZoomExtents(ctx.Doc(), false); } });
}

// TwoPointPerspective: perspective with the camera levelled (eye and target
// at the same height), so vertical world lines stay exactly vertical on
// screen instead of converging to a third vanishing point.
void LevelPerspective(Viewport* vp) {
  CameraState& c = vp->GetCamera().State();
  c.perspective = true;
  Vector3d f = c.target - c.eye;
  const double dist = std::max(f.Length(), 1e-6);
  Vector3d flat(f.x, f.y, 0);
  if (!flat.Unitize()) { flat = Vector3d(1, -1, 0); flat.Unitize(); }
  c.eye = c.target - flat * dist;
  c.up = Vector3d(0, 0, 1);
}

CommandFactory SetMode(DisplayMode mode) {
  return Immediate([mode](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { vp->SetMode(mode); ctx.Print(vp->Name() + " display mode: " + DisplayModeName(mode)); } });
}

// NamedView: "NamedView Save name" / "Restore name" / "Delete name" / "List"
// from the command line; with nothing typed it opens the Named Views panel.
class NamedViewCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto action = ctx.Engine().TakePendingInput();
    if (!action) { ctx.App().Panels().named_views = true; Finish(); return; }
    const std::string a = ToLower(*action);
    std::vector<NamedView>& views = ctx.Doc().NamedViews();
    if (a == "list") {
      ctx.Print(std::to_string(views.size()) + " named view(s)");
      for (const NamedView& v : views) ctx.Print("  " + v.name + ": location " + FormatPoint(v.camera.eye) + ", target " + FormatPoint(v.camera.target));
      Finish();
      return;
    }
    auto name = ctx.Engine().TakePendingInput();
    Viewport* vp = ctx.ActiveViewport();
    if (!name || !vp) { ctx.Warn("NamedView: use Save <name>, Restore <name>, Delete <name> or List"); Finish(); return; }
    auto it = std::find_if(views.begin(), views.end(), [&](const NamedView& v) { return v.name == *name; });
    if (a == "save") {
      NamedView nv{*name, vp->GetCamera().State()};
      if (it != views.end()) *it = nv; else views.push_back(nv);
      ctx.Doc().Touch();
      ctx.Print("Named view '" + *name + "' saved from " + vp->Name());
    } else if (a == "restore") {
      if (it == views.end()) ctx.Warn("No named view '" + *name + "'");
      else { vp->GetCamera().SetState(it->camera); ctx.Print("Named view '" + *name + "' restored in " + vp->Name()); }
    } else if (a == "delete") {
      if (it == views.end()) ctx.Warn("No named view '" + *name + "'");
      else { views.erase(it); ctx.Doc().Touch(); ctx.Print("Named view '" + *name + "' deleted"); }
    } else {
      ctx.Warn("NamedView: unknown option '" + *action + "'");
    }
    Finish();
  }
};

class ZoomCommand : public Command {
 public:
  // `target_only`: ZoomTarget starts directly in the Target sub-mode
  // instead of offering the full Zoom option list.
  explicit ZoomCommand(bool target_only = false) : target_only_(target_only) {}
  void Begin(CommandContext&) override {
    if (target_only_) { WantPoint("New target point (the view dollies to face it, keeping the same distance)"); target_mode_ = true; return; }
    WantPoint("Drag or pick a zoom window corner");
    options = {{"Extents", "", {}, false, false}, {"Selected", "", {}, false, false}, {"All", "", {}, false, false}, {"In", "", {}, false, false}, {"Out", "", {}, false, false}, {"Target", "", {}, false, false}};
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    if (n == "Extents") { vp->PushViewHistory(); vp->ZoomExtents(ctx.Doc(), false); }
    else if (n == "Selected") { vp->PushViewHistory(); vp->ZoomExtents(ctx.Doc(), true); }
    else if (n == "All") { for (auto& v : ctx.Viewports()) v->PushViewHistory(); ctx.ZoomExtentsAll(); }
    else if (n == "In") { vp->PushViewHistory(); vp->GetCamera().Dolly(2.0); }
    else if (n == "Out") { vp->PushViewHistory(); vp->GetCamera().Dolly(-2.0); }
    else if (n == "Target") { WantPoint("Target point"); target_mode_ = true; return; }
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    if (target_mode_) {
      vp->PushViewHistory();
      CameraState& c = vp->GetCamera().State();
      Vector3d d = c.eye - c.target;
      c.target = p; c.eye = p + d;
      ctx.Print("ZoomTarget: new target " + FormatPoint(p) + " in " + vp->Name());
      Finish();
      return;
    }
    if (!a_) { a_ = p; WantPoint("Other corner of zoom window"); return; }
    vp->PushViewHistory();
    kernel::BoundingBox box{Point3d(std::min(a_->x, p.x), std::min(a_->y, p.y), std::min(a_->z, p.z)), Point3d(std::max(a_->x, p.x), std::max(a_->y, p.y), std::max(a_->z, p.z))};
    vp->ZoomTo(box);
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!a_) return;
    ctx.ClearPreview();
    ON_Plane pl = ActivePlane(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(*a_, &u0, &v0); pl.ClosestPointTo(h, &u1, &v1);
    ctx.AddPreviewPolyline({pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)}, true);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::optional<Point3d> a_;
  bool target_mode_ = false;
  bool target_only_ = false;
};

// Pan: "Start of pan" / "End of pan", exactly like dragging the middle
// mouse button - the pixel delta between the two picks is fed to the same
// Camera::Pan the mouse drag uses.
class PanCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Start of pan"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    if (!a_) { a_ = p; ctx.SetLastPoint(p); WantPoint("End of pan"); return; }
    double x0, y0, x1, y1;
    if (vp->WorldToPixel(*a_, x0, y0) && vp->WorldToPixel(p, x1, y1)) {
      vp->PushViewHistory();
      vp->GetCamera().Pan(x1 - x0, y1 - y0, vp->Width(), vp->Height());
      ctx.Print("Pan: " + vp->Name());
    } else {
      ctx.Warn("Pan: pick points visible in the viewport");
    }
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (!a_) return; ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::optional<Point3d> a_;
};

// RotateView: types a rotation angle (degrees) and orbits the view around
// its target by that amount, keeping the target fixed and moving the eye -
// the mouse-drag equivalent of dragging the right button in a perspective
// view. The Vertical option pitches (tilts up/down) instead of the default
// horizontal yaw.
class RotateViewCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantNumber("Rotation angle in degrees (view orbits around its target)", 45.0);
    options = {{"Vertical", "", {}, false, false}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string&) override { if (n == "Vertical") vertical_ = true; }
  void OnNumber(CommandContext& ctx, double degrees) override {
    Viewport* vp = ctx.ActiveViewport();
    if (vp) {
      vp->PushViewHistory();
      if (vertical_) vp->GetCamera().OrbitDegrees(0, degrees); else vp->GetCamera().OrbitDegrees(degrees, 0);
      ctx.Print("RotateView: " + vp->Name() + " rotated " + FormatNumber(degrees) + " degrees" + (vertical_ ? " vertically" : ""));
    }
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { OnNumber(ctx, std::strtod(t.c_str(), nullptr)); }
  bool vertical_ = false;
};

// RotateCamera: like RotateView, but turns the camera itself (the eye stays
// put and the look direction/target turn) instead of orbiting the eye
// around the target - Ctrl+Shift right-drag's typed equivalent.
class RotateCameraCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantNumber("Rotation angle in degrees (the camera turns in place)", 45.0);
    options = {{"Vertical", "", {}, false, false}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string&) override { if (n == "Vertical") vertical_ = true; }
  void OnNumber(CommandContext& ctx, double degrees) override {
    Viewport* vp = ctx.ActiveViewport();
    if (vp) {
      vp->PushViewHistory();
      if (vertical_) vp->GetCamera().TurnInPlace(0, degrees); else vp->GetCamera().TurnInPlace(degrees, 0);
      ctx.Print("RotateCamera: " + vp->Name() + " turned " + FormatNumber(degrees) + " degrees" + (vertical_ ? " vertically" : ""));
    }
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { OnNumber(ctx, std::strtod(t.c_str(), nullptr)); }
  bool vertical_ = false;
};

class CPlaneCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    WantPoint("CPlane origin");
    options = {{"World", "", {}, false, false}, {"View", "", {}, false, false}, {"Top", "", {}, false, false}, {"Front", "", {}, false, false}, {"Right", "", {}, false, false}};
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    ConstructionPlane& cp = vp->CPlane();
    if (n == "World" || n == "Top") cp = ConstructionPlane{};
    else if (n == "Front") { cp.origin = Point3d(0, 0, 0); cp.x_axis = Vector3d(1, 0, 0); cp.y_axis = Vector3d(0, 0, 1); }
    else if (n == "Right") { cp.origin = Point3d(0, 0, 0); cp.x_axis = Vector3d(0, 1, 0); cp.y_axis = Vector3d(0, 0, 1); }
    else if (n == "View") { cp.origin = vp->GetCamera().State().target; cp.x_axis = vp->GetCamera().Right(); cp.y_axis = vp->GetCamera().Up(); }
    ctx.Print("CPlane set to " + n);
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) { WantPoint("X axis direction. Press Enter to keep the current orientation"); return; }
    if (pts_.size() == 2) { WantPoint("Y axis direction. Press Enter for the CPlane normal"); return; }
    ConstructionPlane& cp = vp->CPlane();
    Vector3d x = pts_[1] - pts_[0], y = pts_[2] - pts_[0];
    if (x.Unitize() && y.Unitize()) { Vector3d z = ON_CrossProduct(x, y); if (z.Unitize()) { cp.origin = pts_[0]; cp.x_axis = x; cp.y_axis = ON_CrossProduct(z, x); } }
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    Viewport* vp = ctx.ActiveViewport();
    if (vp && !pts_.empty()) {
      ConstructionPlane& cp = vp->CPlane();
      cp.origin = pts_[0];
      if (pts_.size() >= 2) { Vector3d x = pts_[1] - pts_[0]; if (x.Unitize()) { Vector3d z = cp.Normal(); cp.x_axis = x; cp.y_axis = ON_CrossProduct(z, x); cp.y_axis.Unitize(); } }
    }
    Finish();
  }
  std::vector<Point3d> pts_;
};

}  // namespace

void RegisterViewCommands(CommandEngine& e) {
  Reg(e, "Zoom", Make<ZoomCommand>());
  Reg(e, "ZoomExtents", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { vp->PushViewHistory(); vp->ZoomExtents(ctx.Doc(), false); } }));
  Reg(e, "ZoomExtentsAll", Immediate([](CommandContext& ctx) { for (auto& vp : ctx.Viewports()) vp->PushViewHistory(); ctx.ZoomExtentsAll(); }));
  Reg(e, "ZoomSelected", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { vp->PushViewHistory(); vp->ZoomExtents(ctx.Doc(), true); } }));
  Reg(e, "ZoomSelectedAll", Immediate([](CommandContext& ctx) { for (auto& vp : ctx.Viewports()) { vp->PushViewHistory(); vp->ZoomExtents(ctx.Doc(), true); } }));
  Reg(e, "ZoomWindow", Make<ZoomCommand>());
  Reg(e, "ZoomTarget", Make<ZoomCommand>(true));
  // Zoom1To1 is registered here too, but RegisterViewToolsCommands (which
  // runs after RegisterViewCommands) replaces it with a calibratable
  // version (Zoom1To1Calibrate) - no need for a second, dead definition.
  Reg(e, "Top", SetView("Top"));
  Reg(e, "Bottom", SetView("Bottom"));
  Reg(e, "Front", SetView("Front"));
  Reg(e, "Back", SetView("Back"));
  Reg(e, "Right", SetView("Right"));
  Reg(e, "Left", SetView("Left"));
  Reg(e, "Perspective", SetView("Perspective"));
  Reg(e, "Isometric", SetView("Isometric"));
  Reg(e, "Plan", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { vp->PushViewHistory(); vp->SetStandardView("Top"); vp->ZoomExtents(ctx.Doc(), false); } }));
  Reg(e, "TwoPointPerspective", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { vp->PushViewHistory(); LevelPerspective(vp); ctx.Print("TwoPointPerspective: " + vp->Name() + " levelled (vertical lines stay vertical)"); } }));
  Reg(e, "UndoView", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        if (vp->UndoView()) ctx.Print("UndoView: restored the previous view in " + vp->Name());
        else ctx.Warn("UndoView: no earlier view in " + vp->Name() + " to restore");
      }));
  Reg(e, "RedoView", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        if (vp->RedoView()) ctx.Print("RedoView: restored the view undone by UndoView in " + vp->Name());
        else ctx.Warn("RedoView: no undone view to restore in " + vp->Name());
      }));
  Reg(e, "Pan", Make<PanCommand>());
  Reg(e, "RotateView", Make<RotateViewCommand>());
  Reg(e, "RotateCamera", Make<RotateCameraCommand>());
  Reg(e, "TiltView", Immediate([](CommandContext& ctx) {
        double degrees = 15.0;
        if (auto t = ctx.Engine().TakePendingInput()) degrees = std::strtod(t->c_str(), nullptr);
        if (Viewport* vp = ctx.ActiveViewport()) {
          vp->PushViewHistory();
          vp->GetCamera().RotateAboutViewAxis(degrees);
          ctx.Print("TiltView: " + vp->Name() + " tilted " + FormatNumber(degrees) + " degrees");
        }
      }), CommandStatus::Implemented, "Tilts by 15 degrees per call by default; pass a number of degrees to override.");
  auto turntable = [](CommandContext& ctx, const char* label, const char* default_play_args) {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) return;
    vp->PushViewHistory();
    std::string set_args, play_args = default_play_args;
    bool saw_loops = false;
    while (auto t = ctx.Engine().TakePendingInput()) {
      const size_t eq = t->find('=');
      if (eq != std::string::npos && ToLower(t->substr(0, eq)) == "loops") { play_args = " " + *t; saw_loops = true; }
      else set_args += " " + *t;
    }
    (void)saw_loops;
    ctx.Engine().Execute(std::string("SetTurntableAnimation") + set_args);
    ctx.Engine().Execute(std::string("PlayAnimation") + play_args);
    ctx.Print(std::string(label) + ": " + vp->Name());
  };
  Reg(e, "Spin", Immediate([turntable](CommandContext& ctx) { turntable(ctx, "Spin", " Loops=3"); }), CommandStatus::Implemented,
      "Builds a turntable camera animation (SetTurntableAnimation) and plays it (PlayAnimation), 3 loops by default; pass Loops=/Frames=/Degrees=/Direction= to shape the spin.");
  Reg(e, "Turntable", Immediate([turntable](CommandContext& ctx) { turntable(ctx, "Turntable", ""); }), CommandStatus::Implemented,
      "Builds a turntable camera animation (SetTurntableAnimation: Frames=/Degrees=/Direction=) and plays it once (PlayAnimation: Loops=).");
  Reg(e, "4View", Immediate([](CommandContext& ctx) { ctx.App().SetViewportLayout(4); }));
  Reg(e, "3View", Immediate([](CommandContext& ctx) { ctx.App().SetViewportLayout(3); }));
  Reg(e, "MaxViewport", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { vp->SetMaximized(!vp->Maximized()); ctx.Print(vp->Name() + (vp->Maximized() ? " maximized" : " restored")); } }));
  Reg(e, "NewViewport", Immediate([](CommandContext& ctx) {
        Viewport* nv = ctx.App().AddViewport("Viewport", "Perspective", false);
        ctx.Print("NewViewport: added " + nv->Name());
      }));
  Reg(e, "NextViewport", Immediate([](CommandContext& ctx) { auto& v = ctx.Viewports(); for (size_t i = 0; i < v.size(); ++i) if (v[i]->IsActive()) { v[i]->SetActive(false); v[(i + 1) % v.size()]->SetActive(true); return; } }));
  Reg(e, "PrevViewport", Immediate([](CommandContext& ctx) { auto& v = ctx.Viewports(); for (size_t i = 0; i < v.size(); ++i) if (v[i]->IsActive()) { v[i]->SetActive(false); v[(i + v.size() - 1) % v.size()]->SetActive(true); return; } }));
  Reg(e, "SetView", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        auto arg = ctx.Engine().TakePendingInput();
        if (!arg) { ctx.App().Panels().named_views = true; return; }
        static const char* kStd[] = {"Top", "Bottom", "Front", "Back", "Right", "Left", "Perspective", "Isometric"};
        for (const char* s : kStd) {
          if (ToLower(*arg) == ToLower(std::string(s))) {
            vp->PushViewHistory();
            vp->SetStandardView(s);
            vp->ZoomExtents(ctx.Doc(), false);
            ctx.Print("SetView: " + vp->Name() + " set to " + s);
            return;
          }
        }
        for (const NamedView& nv : ctx.Doc().NamedViews()) {
          if (nv.name == *arg) {
            vp->PushViewHistory();
            vp->GetCamera().SetState(nv.camera);
            ctx.Print("SetView: " + vp->Name() + " restored named view '" + *arg + "'");
            return;
          }
        }
        ctx.Warn("SetView: unknown view '" + *arg + "' (use Top/Bottom/Front/Back/Right/Left/Perspective/Isometric, or a saved named view)");
      }));
  Reg(e, "NamedView", Make<NamedViewCommand>());
  Reg(e, "SetDisplayMode", Immediate([](CommandContext& ctx) { ctx.App().Panels().display = true; }));
  Reg(e, "Wireframe", SetMode(DisplayMode::Wireframe));
  Reg(e, "Shade", SetMode(DisplayMode::Shaded));
  Reg(e, "ShadedViewport", SetMode(DisplayMode::Shaded));
  Reg(e, "RenderedViewport", SetMode(DisplayMode::Rendered));
  Reg(e, "GhostedViewport", SetMode(DisplayMode::Ghosted));
  Reg(e, "XRayViewport", SetMode(DisplayMode::XRay));
  Reg(e, "TechnicalViewport", SetMode(DisplayMode::Technical));
  Reg(e, "ArtisticViewport", SetMode(DisplayMode::Artistic));
  Reg(e, "PenViewport", SetMode(DisplayMode::Pen));
  Reg(e, "ArcticViewport", SetMode(DisplayMode::Arctic));
  Reg(e, "MonochromeViewport", SetMode(DisplayMode::Monochrome));
  Reg(e, "RayTracedViewport", SetMode(DisplayMode::RayTraced), CommandStatus::Implemented,
      "Progressive CPU path tracer at 1/4 viewport resolution, accumulating while the camera is still.");
  Reg(e, "Render", SetMode(DisplayMode::Rendered), CommandStatus::Partial, "Switches the viewport to Rendered mode; RegisterRaytraceCommands replaces this with the real offline path tracer.");
  Reg(e, "RenderPreview", SetMode(DisplayMode::Rendered), CommandStatus::Partial);
  Reg(e, "RefreshShade", Immediate([](CommandContext& ctx) { for (SceneObject& o : ctx.Doc().Objects()) o.InvalidateDisplay(); }));
  Reg(e, "ClearAllMeshes", Immediate([](CommandContext& ctx) { for (SceneObject& o : ctx.Doc().Objects()) o.InvalidateDisplay(); }));
  Reg(e, "Grid", Immediate([](CommandContext& ctx) { ctx.Settings().show_grid = !ctx.Settings().show_grid; ctx.Print(std::string("Grid ") + (ctx.Settings().show_grid ? "on" : "off")); }));
  Reg(e, "GridOptions", Immediate([](CommandContext& ctx) { ctx.App().Panels().document_properties = true; }));
  Reg(e, "CPlane", Make<CPlaneCommand>());
  Reg(e, "CPlaneToWorld", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->CPlane() = ConstructionPlane{}; }));
  Reg(e, "CPlaneToView", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { ConstructionPlane& cp = vp->CPlane(); cp.origin = vp->GetCamera().State().target; cp.x_axis = vp->GetCamera().Right(); cp.y_axis = vp->GetCamera().Up(); } }));
  Reg(e, "CPlaneThroughPoint", Make<PointsCommand>(std::vector<std::string>{"Point for CPlane origin"}, [](CommandContext& ctx, const std::vector<Point3d>& p) { if (Viewport* vp = ctx.ActiveViewport()) vp->CPlane().origin = p[0]; }));
  Reg(e, "CPlaneToObject", OnSelection("Select a planar curve or surface", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          ON_Plane pl;
          bool ok = false;
          if (o->kind == ObjectKind::Curve) ok = o->curve->raw().IsPlanar(&pl, 1e-6);
          else if (o->kind == ObjectKind::Surface) { ok = o->surface->raw().IsPlanar(&pl, 1e-6); if (!ok) { kernel::Interval du = o->surface->Domain(0), dv = o->surface->Domain(1); double u = (du.min + du.max) / 2, v = (dv.min + dv.max) / 2; pl = ON_Plane(o->surface->PointAt(u, v), o->surface->NormalAt(u, v)); ok = true; } }
          if (ok) { vp->CPlane().origin = pl.origin; vp->CPlane().x_axis = pl.xaxis; vp->CPlane().y_axis = pl.yaxis; ctx.Print("CPlane aligned to object"); return; }
        }
        ctx.Warn("No planar object found");
      }));
  Reg(e, "ViewportProperties", Immediate([](CommandContext& ctx) { ctx.App().Panels().display = true; }));
  Reg(e, "ViewCaptureToFile", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        auto save = [vp, &ctx](const std::string& path) {
          std::string p = path;
          if (p.size() < 4 || ToLower(p.substr(p.size() - 4)) != ".bmp") p += ".bmp";
          std::string err;
          if (vp->CaptureToFile(p, err)) ctx.App().Notify("Saved " + p); else ctx.App().Notify(err);
        };
        if (auto p = ctx.Engine().TakePendingInput()) { save(*p); return; }
        ctx.App().ShowFileDialog("Save viewport image", {".bmp"}, true, save);
      }));
  Reg(e, "ScreenCaptureToFile", Immediate([](CommandContext& ctx) {
        auto save = [&ctx](const std::string& path) {
          std::string p = path;
          if (p.size() < 4 || ToLower(p.substr(p.size() - 4)) != ".bmp") p += ".bmp";
          ctx.App().RequestWindowCapture(p);
        };
        if (auto p = ctx.Engine().TakePendingInput()) { save(*p); return; }
        ctx.App().ShowFileDialog("Save the application window", {".bmp"}, true, save);
      }), CommandStatus::Implemented, "Captures the whole application window (every viewport and panel as composited), not just the active viewport.");
}

}  // namespace dino8::app
