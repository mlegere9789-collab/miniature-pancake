// Selection commands.
#include "commands/cmd_common.h"

namespace dino8::app {

namespace {

CommandFactory SelKind(ObjectKind kind) {
  return Immediate([kind](CommandContext& ctx) {
    ctx.Doc().SelectWhere([kind, &ctx](const SceneObject& o) { return o.kind == kind && ctx.Doc().IsObjectVisible(o) && !ctx.Doc().IsObjectLocked(o); }, true);
    ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) selected");
  });
}

CommandFactory SelWhere(std::function<bool(const SceneObject&)> pred) {
  return Immediate([pred](CommandContext& ctx) {
    ctx.Doc().SelectWhere([&](const SceneObject& o) { return pred(o) && ctx.Doc().IsObjectVisible(o) && !ctx.Doc().IsObjectLocked(o); }, true);
    ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) selected");
  });
}

class SelWindowCommand : public Command {
 public:
  explicit SelWindowCommand(bool crossing) : crossing_(crossing) {}
  void Begin(CommandContext&) override { WantPoint("First corner of selection window"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!a_) { a_ = p; WantPoint("Other corner"); return; }
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    double x0, y0, x1, y1;
    if (vp->WorldToPixel(*a_, x0, y0) && vp->WorldToPixel(p, x1, y1)) {
      for (ObjectId id : vp->ObjectsInWindow(ctx.Doc(), std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1), crossing_)) ctx.Doc().Select(id, true);
      ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) selected");
    }
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
  bool crossing_;
  std::optional<Point3d> a_;
};

}  // namespace

void RegisterSelectCommands(CommandEngine& e) {
  Reg(e, "SelAll", Immediate([](CommandContext& ctx) { ctx.Doc().SelectWhere([&](const SceneObject& o) { return ctx.Doc().IsObjectVisible(o) && !ctx.Doc().IsObjectLocked(o); }); ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) selected"); }));
  Reg(e, "SelNone", Immediate([](CommandContext& ctx) { ctx.Doc().SelectNone(); }));
  Reg(e, "Invert", Immediate([](CommandContext& ctx) { ctx.Doc().InvertSelection(); ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) selected"); }));
  Reg(e, "SelPt", SelKind(ObjectKind::Point));
  Reg(e, "SelCrv", SelKind(ObjectKind::Curve));
  Reg(e, "SelSrf", SelKind(ObjectKind::Surface));
  Reg(e, "SelPolysrf", SelKind(ObjectKind::Brep));
  Reg(e, "SelMesh", SelKind(ObjectKind::Mesh));
  Reg(e, "SelSubD", SelKind(ObjectKind::SubD));
  Reg(e, "SelClosedCrv", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Curve && o.curve->IsClosed(); }));
  Reg(e, "SelOpenCrv", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Curve && !o.curve->IsClosed(); }));
  Reg(e, "SelClosedSrf", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Surface && (o.surface->IsClosed(0) || o.surface->IsClosed(1)); }));
  Reg(e, "SelOpenSrf", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Surface && !(o.surface->IsClosed(0) || o.surface->IsClosed(1)); }));
  Reg(e, "SelClosedPolysrf", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Brep && o.brep->raw().IsSolid(); }));
  Reg(e, "SelOpenPolysrf", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Brep && !o.brep->raw().IsSolid(); }));
  Reg(e, "SelClosedMesh", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Mesh && o.mesh->IsClosedManifold(); }));
  Reg(e, "SelOpenMesh", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Mesh && !o.mesh->IsClosedManifold(); }));
  Reg(e, "SelLine", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Curve && o.curve->IsLinear(); }));
  Reg(e, "SelPolyline", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Curve && o.curve->Degree() == 1; }));
  Reg(e, "SelCircle", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Curve && o.curve->IsCircle(); }));
  Reg(e, "SelArc", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Curve && o.curve->IsArc() && !o.curve->IsCircle(); }));
  Reg(e, "SelPlanarCrv", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Curve && o.curve->IsPlanar(1e-6); }));
  Reg(e, "SelPlanarSrf", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::Surface && o.surface->IsPlanar(1e-6); }));
  Reg(e, "SelGroup", SelWhere([](const SceneObject& o) { return o.group_id >= 0; }));
  Reg(e, "SelName", Immediate([](CommandContext& ctx) {
        std::string name;
        for (const SceneObject& o : ctx.Doc().Objects()) if (o.selected && !o.name.empty()) { name = o.name; break; }
        if (name.empty()) { ctx.Warn("Select a named object first"); return; }
        ctx.Doc().SelectWhere([&](const SceneObject& o) { return o.name == name; });
        ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) named '" + name + "' selected");
      }), CommandStatus::Partial, "Selects objects sharing the first selected object's name.");
  Reg(e, "SelLayer", Immediate([](CommandContext& ctx) {
        int layer = ctx.Doc().CurrentLayer();
        for (const SceneObject& o : ctx.Doc().Objects()) if (o.selected) { layer = o.layer_index; break; }
        ctx.Doc().SelectWhere([&](const SceneObject& o) { return o.layer_index == layer && ctx.Doc().IsObjectVisible(o); });
        ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) on layer " + ctx.Doc().LayerFullPath(layer));
      }));
  Reg(e, "SelColor", Immediate([](CommandContext& ctx) {
        const SceneObject* ref = nullptr;
        for (const SceneObject& o : ctx.Doc().Objects()) if (o.selected) { ref = &o; break; }
        if (!ref) { ctx.Warn("Select an object with the colour to match"); return; }
        Color c = ctx.Doc().EffectiveColor(*ref);
        ctx.Doc().SelectWhere([&](const SceneObject& o) { Color oc = ctx.Doc().EffectiveColor(o); return std::fabs(oc.r - c.r) < 0.01 && std::fabs(oc.g - c.g) < 0.01 && std::fabs(oc.b - c.b) < 0.01; });
      }));
  Reg(e, "SelLast", Immediate([](CommandContext& ctx) { if (!ctx.Doc().Objects().empty()) { ctx.Doc().SelectNone(); ctx.Doc().Select(ctx.Doc().Objects().back().id, true); } }));
  Reg(e, "SelPrev", Immediate([](CommandContext& ctx) { static std::vector<ObjectId> prev; std::vector<ObjectId> cur = ctx.Doc().SelectedIds(); if (!cur.empty()) prev = cur; else for (ObjectId id : prev) ctx.Doc().Select(id, true); }), CommandStatus::Partial);
  Reg(e, "SelDup", Immediate([](CommandContext& ctx) {
        ctx.Doc().SelectNone();
        const auto& objs = ctx.Doc().Objects();
        int n = 0;
        for (size_t i = 0; i < objs.size(); ++i) for (size_t j = 0; j < i; ++j) {
          if (objs[i].kind != objs[j].kind) continue;
          kernel::BoundingBox a = objs[i].BoundingBox(), b = objs[j].BoundingBox();
          if ((a.min - b.min).Length() < 1e-6 && (a.max - b.max).Length() < 1e-6 && objs[i].Describe() == objs[j].Describe()) { ctx.Doc().Select(objs[i].id, true); ++n; break; }
        }
        ctx.Print(std::to_string(n) + " duplicate(s) selected");
      }));
  Reg(e, "SelSmall", Immediate([](CommandContext& ctx) {
        const double lim = ctx.Settings().grid_spacing;
        ctx.Doc().SelectWhere([&](const SceneObject& o) { kernel::BoundingBox b = o.BoundingBox(); return o.kind != ObjectKind::Point && (b.max - b.min).Length() < lim; });
        ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " small object(s) selected");
      }), CommandStatus::Partial, "Threshold is one grid unit.");
  Reg(e, "SelVisible", Immediate([](CommandContext& ctx) { ctx.Doc().SelectWhere([&](const SceneObject& o) { return ctx.Doc().IsObjectVisible(o); }); }));
  Reg(e, "SelLocked", Immediate([](CommandContext& ctx) { ctx.Doc().SelectWhere([&](const SceneObject& o) { return ctx.Doc().IsObjectLocked(o); }); }));
  Reg(e, "SelHidden", Immediate([](CommandContext& ctx) { ctx.Doc().SelectWhere([&](const SceneObject& o) { return !o.visible; }); }));
  Reg(e, "SelWindow", Make<SelWindowCommand>(false));
  Reg(e, "SelCrossing", Make<SelWindowCommand>(true));
  Reg(e, "SelBox", Make<SelWindowCommand>(false), CommandStatus::Partial, "Window selection in the active view.");
  Reg(e, "SelectionFilter", Immediate([](CommandContext& ctx) { ctx.App().Panels().selection_filter = true; }));
  Reg(e, "SelID", Immediate([](CommandContext& ctx) { for (ObjectId id : ctx.Doc().SelectedIds()) ctx.Print("Object id " + std::to_string(id)); }), CommandStatus::Partial, "Lists the ids of selected objects; typed-id selection is planned.");
  Reg(e, "SelBoundary", Immediate([](CommandContext& ctx) { ctx.Doc().SelectWhere([](const SceneObject& o) { return o.kind == ObjectKind::Curve && o.curve->IsClosed(); }); }), CommandStatus::Partial, "Selects closed curves.");
  Reg(e, "SelChain", Immediate([](CommandContext& ctx) {
        std::vector<ObjectId> sel = ctx.Doc().SelectedIds();
        if (sel.empty()) { ctx.Warn("Select a curve first"); return; }
        bool grew = true;
        const double tol = ctx.Settings().absolute_tolerance * 10;
        while (grew) {
          grew = false;
          for (SceneObject& o : ctx.Doc().Objects()) {
            if (o.selected || o.kind != ObjectKind::Curve) continue;
            for (const SceneObject& s : ctx.Doc().Objects()) {
              if (!s.selected || s.kind != ObjectKind::Curve) continue;
              const ON_NurbsCurve& a = o.curve->raw(); const ON_NurbsCurve& b = s.curve->raw();
              if (a.PointAtStart().DistanceTo(b.PointAtEnd()) < tol || a.PointAtEnd().DistanceTo(b.PointAtStart()) < tol || a.PointAtStart().DistanceTo(b.PointAtStart()) < tol || a.PointAtEnd().DistanceTo(b.PointAtEnd()) < tol) { o.selected = true; grew = true; break; }
            }
          }
        }
        ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " chained curve(s) selected");
      }));
}

}  // namespace dino8::app
