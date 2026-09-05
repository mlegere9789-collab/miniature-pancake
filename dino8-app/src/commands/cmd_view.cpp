// View, viewport, display-mode and CPlane commands.
#include "commands/cmd_common.h"

namespace dino8::app {

namespace {

CommandFactory SetView(const char* view) {
  return Immediate([view](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { vp->SetStandardView(view); vp->ZoomExtents(ctx.Doc(), false); } });
}

CommandFactory SetMode(DisplayMode mode) {
  return Immediate([mode](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->SetMode(mode); });
}

class ZoomCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    WantPoint("Drag or pick a zoom window corner");
    options = {{"Extents", "", {}, false, false}, {"Selected", "", {}, false, false}, {"All", "", {}, false, false}, {"In", "", {}, false, false}, {"Out", "", {}, false, false}, {"Target", "", {}, false, false}};
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    if (n == "Extents") vp->ZoomExtents(ctx.Doc(), false);
    else if (n == "Selected") vp->ZoomExtents(ctx.Doc(), true);
    else if (n == "All") ctx.ZoomExtentsAll();
    else if (n == "In") vp->GetCamera().Dolly(2.0);
    else if (n == "Out") vp->GetCamera().Dolly(-2.0);
    else if (n == "Target") { WantPoint("Target point"); target_mode_ = true; return; }
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    if (target_mode_) { CameraState& c = vp->GetCamera().State(); Vector3d d = c.eye - c.target; c.target = p; c.eye = p + d; Finish(); return; }
    if (!a_) { a_ = p; WantPoint("Other corner of zoom window"); return; }
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
  Reg(e, "ZoomExtents", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->ZoomExtents(ctx.Doc(), false); }));
  Reg(e, "ZoomExtentsAll", Immediate([](CommandContext& ctx) { ctx.ZoomExtentsAll(); }));
  Reg(e, "ZoomSelected", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->ZoomExtents(ctx.Doc(), true); }));
  Reg(e, "ZoomSelectedAll", Immediate([](CommandContext& ctx) { for (auto& vp : ctx.Viewports()) vp->ZoomExtents(ctx.Doc(), true); }));
  Reg(e, "ZoomWindow", Make<ZoomCommand>());
  Reg(e, "ZoomTarget", Make<ZoomCommand>(), CommandStatus::Partial, "Use the Target option.");
  Reg(e, "Zoom1To1", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { CameraState& c = vp->GetCamera().State(); c.ortho_height = vp->Height() * 0.2646; } }), CommandStatus::Partial, "Assumes a 96 dpi screen.");
  Reg(e, "Top", SetView("Top"));
  Reg(e, "Bottom", SetView("Bottom"));
  Reg(e, "Front", SetView("Front"));
  Reg(e, "Back", SetView("Back"));
  Reg(e, "Right", SetView("Right"));
  Reg(e, "Left", SetView("Left"));
  Reg(e, "Perspective", SetView("Perspective"));
  Reg(e, "Isometric", SetView("Isometric"));
  Reg(e, "Plan", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) { vp->SetStandardView("Top"); vp->ZoomExtents(ctx.Doc(), false); } }));
  Reg(e, "TwoPointPerspective", SetView("Perspective"), CommandStatus::Partial, "Uses the standard perspective projection.");
  Reg(e, "UndoView", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->SetStandardView(vp->StandardView()); }), CommandStatus::Partial, "Restores the viewport's standard view.");
  Reg(e, "RedoView", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->ZoomExtents(ctx.Doc(), false); }), CommandStatus::Partial);
  Reg(e, "Pan", Immediate([](CommandContext& ctx) { ctx.Print("Pan: drag with the middle mouse button, or Shift + right mouse button."); }), CommandStatus::Partial, "Interactive pan is always available with the mouse.");
  Reg(e, "RotateView", Immediate([](CommandContext& ctx) { ctx.Print("RotateView: drag with the right mouse button in a perspective view, or use the arrow keys."); }), CommandStatus::Partial);
  Reg(e, "RotateCamera", Immediate([](CommandContext& ctx) { ctx.Print("RotateCamera: Ctrl + Shift + right-drag."); }), CommandStatus::Partial);
  Reg(e, "TiltView", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->GetCamera().RotateAboutViewAxis(15); }), CommandStatus::Partial, "Tilts by 15 degrees per call.");
  Reg(e, "Spin", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->GetCamera().Orbit(120, 0); }), CommandStatus::Partial, "Orbits one step; continuous spin is planned.");
  Reg(e, "Turntable", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->GetCamera().Orbit(60, 0); }), CommandStatus::Partial);
  Reg(e, "4View", Immediate([](CommandContext& ctx) { ctx.App().SetViewportLayout(4); }));
  Reg(e, "3View", Immediate([](CommandContext& ctx) { ctx.App().SetViewportLayout(3); }));
  Reg(e, "MaxViewport", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->SetMaximized(!vp->Maximized()); }));
  Reg(e, "NewViewport", Immediate([](CommandContext& ctx) { ctx.App().SetViewportLayout(4); }), CommandStatus::Partial, "Restores the 4-viewport layout; ad-hoc viewports are planned.");
  Reg(e, "NextViewport", Immediate([](CommandContext& ctx) { auto& v = ctx.Viewports(); for (size_t i = 0; i < v.size(); ++i) if (v[i]->IsActive()) { v[i]->SetActive(false); v[(i + 1) % v.size()]->SetActive(true); return; } }));
  Reg(e, "PrevViewport", Immediate([](CommandContext& ctx) { auto& v = ctx.Viewports(); for (size_t i = 0; i < v.size(); ++i) if (v[i]->IsActive()) { v[i]->SetActive(false); v[(i + v.size() - 1) % v.size()]->SetActive(true); return; } }));
  Reg(e, "SetView", SetView("Perspective"), CommandStatus::Partial, "Use Top/Front/Right/Perspective or the viewport title menu.");
  Reg(e, "NamedView", Immediate([](CommandContext& ctx) { ctx.App().Panels().named_views = true; }));
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
  Reg(e, "RayTracedViewport", SetMode(DisplayMode::Rendered), CommandStatus::Partial, "Uses the Rendered mode; path tracing is planned.");
  Reg(e, "Render", SetMode(DisplayMode::Rendered), CommandStatus::Partial, "Switches the viewport to Rendered mode; offline rendering is planned.");
  Reg(e, "RenderPreview", SetMode(DisplayMode::Rendered), CommandStatus::Partial);
  Reg(e, "RefreshShade", Immediate([](CommandContext& ctx) { for (SceneObject& o : ctx.Doc().Objects()) o.InvalidateDisplay(); }));
  Reg(e, "ClearAllMeshes", Immediate([](CommandContext& ctx) { for (SceneObject& o : ctx.Doc().Objects()) o.InvalidateDisplay(); }));
  Reg(e, "Grid", Immediate([](CommandContext& ctx) { ctx.Settings().show_grid = !ctx.Settings().show_grid; }));
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
  Reg(e, "ViewCaptureToFile", Immediate([](CommandContext& ctx) { ctx.Print("ViewCaptureToFile: use your OS screenshot tool for now; direct PNG capture is planned."); }), CommandStatus::Partial);
  Reg(e, "ClippingPlane", Immediate([](CommandContext& ctx) { ctx.Print("ClippingPlane: use Split to cut solids; live clipping is planned."); }), CommandStatus::Partial);
}

}  // namespace dino8::app
