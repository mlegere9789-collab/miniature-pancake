// Editing commands: Delete, Undo/Redo, Group, Hide/Show/Lock, Join,
// Explode, Rebuild, Reverse, Offset...
#include "commands/cmd_common.h"
#include "io/File3dm.h"

#include <filesystem>
#include <limits>

namespace dino8::app {

namespace {

void HideShow(CommandContext& ctx, const std::vector<ObjectId>& ids, bool visible, const char* label) {
  ctx.Doc().BeginChange(label);
  for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->visible = visible; if (!visible) o->selected = false; }
}


// Rebuild with a point count and degree.
class RebuildCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves or surfaces to rebuild"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantNumber("Point count", 16);
    options = {{"Degree", std::to_string(degree_), {}, true, false}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Degree") { int d = std::atoi(v.c_str()); if (d >= 1 && d <= 11) degree_ = d; options[0].value = std::to_string(degree_); } }
  void OnNumber(CommandContext& ctx, double v) override {
    const int n = std::max(2, static_cast<int>(v));
    ctx.Doc().BeginChange("Rebuild");
    for (ObjectId id : ids_) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      const int deg = std::min(degree_, n - 1);
      if (o->kind == ObjectKind::Curve) {
        std::vector<Point3d> pts;
        kernel::Interval d = o->curve->Domain();
        const bool closed = o->curve->IsClosed();
        for (int i = 0; i < n; ++i) pts.push_back(o->curve->PointAt(d.min + (d.max - d.min) * i / (closed ? n : n - 1.0)));
        if (closed) pts.push_back(pts.front());
        *o->curve = kernel::NurbsCurve::FromControlPoints(pts, deg);
        o->InvalidateDisplay();
      } else if (o->kind == ObjectKind::Surface) {
        std::vector<Point3d> grid;
        kernel::Interval du = o->surface->Domain(0), dv = o->surface->Domain(1);
        for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) grid.push_back(o->surface->PointAt(du.min + (du.max - du.min) * i / (n - 1.0), dv.min + (dv.max - dv.min) * j / (n - 1.0)));
        *o->surface = kernel::NurbsSurface::FromControlGrid(grid, n, n, deg, deg);
        o->InvalidateDisplay();
      }
    }
    Finish();
  }
  std::vector<ObjectId> ids_;
  int degree_ = 3;
};

// Raises curves/surfaces to a typed target degree (never lowers it: degree
// reduction is a distinct, lossy fitting operation, not plain elevation).
class ChangeDegreeCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves or surfaces"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    int cur = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (o && o->kind == ObjectKind::Curve) cur = std::max(cur, o->curve->Degree());
      else if (o && o->kind == ObjectKind::Surface) cur = std::max({cur, o->surface->DegreeU(), o->surface->DegreeV()});
    }
    WantNumber("New degree", std::max(3, cur));
  }
  void OnNumber(CommandContext& ctx, double v) override {
    const int target = std::clamp(static_cast<int>(v + 0.5), 1, 11);
    ctx.Doc().BeginChange("ChangeDegree");
    int done = 0;
    for (ObjectId id : ids_) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (o->kind == ObjectKind::Curve) {
        if (target > o->curve->Degree() && o->curve->ElevateDegree(target) != kernel::Result::Failed) { o->InvalidateDisplay(); ++done; }
      } else if (o->kind == ObjectKind::Surface) {
        bool changed = false;
        if (target > o->surface->DegreeU() && o->surface->ElevateDegree(0, target) != kernel::Result::Failed) changed = true;
        if (target > o->surface->DegreeV() && o->surface->ElevateDegree(1, target) != kernel::Result::Failed) changed = true;
        if (changed) { o->InvalidateDisplay(); ++done; }
      }
    }
    ctx.Print("ChangeDegree: " + std::to_string(done) + " object(s) elevated to degree " + std::to_string(target));
    Finish();
  }
  std::vector<ObjectId> ids_;
};

// Offset curves by a typed distance or a picked side point.
class OffsetCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to offset"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) ids_.push_back(id); }
    if (ids_.empty()) { ctx.Warn("Select curves"); Finish(); return; }
    WantPoint("Side to offset, or type a distance");
    options = {{"Distance", FormatNumber(distance_), {}, true, false}, {"BothSides", "No", {"Yes", "No"}, false, true}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Distance") { double d = std::atof(v.c_str()); if (d > 0) distance_ = d; options[0].value = FormatNumber(distance_); }
    if (n == "BothSides") { both_ = !both_; options[1].value = both_ ? "Yes" : "No"; }
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* e; double v = std::strtod(t.c_str(), &e);
    if (e && !*e && v > 0) { distance_ = v; options[0].value = FormatNumber(distance_); ctx.Print("Distance=" + FormatNumber(v) + ". Pick the side."); }
  }
  void OnPoint(CommandContext& ctx, Point3d side) override {
    ctx.ClearPreview();
    ctx.Doc().BeginChange("Offset");
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      const int sign = SideSign(ctx, *o->curve, side);
      Make(ctx, *o->curve, distance_ * sign, o->layer_index);
      if (both_) Make(ctx, *o->curve, -distance_ * sign, o->layer_index);
    }
    Finish();
  }
  int SideSign(CommandContext& ctx, const kernel::NurbsCurve& c, Point3d side) {
    const double t = c.ClosestPointParameter(side);
    Vector3d tan = c.TangentAt(t);
    Vector3d n = ON_CrossProduct(tan, ActiveNormal(ctx));
    return ON_DotProduct(side - c.PointAt(t), n) >= 0 ? 1 : -1;
  }
  void Make(CommandContext& ctx, const kernel::NurbsCurve& c, double d, int layer) {
    std::vector<Point3d> pts;
    Vector3d up = ActiveNormal(ctx);
    if (c.IsLinear()) {
      kernel::Interval dom = c.Domain();
      Vector3d tan = c.TangentAt(dom.min); Vector3d s = ON_CrossProduct(tan, up); s.Unitize();
      SceneObject n = SceneObject::MakeCurve(PolylineCurve({c.PointAt(dom.min) + s * d, c.PointAt(dom.max) + s * d}));
      n.layer_index = layer; ctx.Doc().Add(std::move(n));
      return;
    }
    if (c.IsCircle()) {
      // Exact: a concentric circle.
      ON_Arc arc;
      if (c.raw().IsArc(nullptr, &arc)) {
        ON_Circle circ = arc;
        kernel::Interval dom = c.Domain();
        Vector3d tan = c.TangentAt(dom.min); Vector3d s = ON_CrossProduct(tan, up); s.Unitize();
        const double newr = circ.radius + (ON_DotProduct(s, c.PointAt(dom.min) - circ.Center()) > 0 ? d : -d);
        if (newr > 0) { ON_ArcCurve ac(ON_Circle(circ.plane, newr)); kernel::NurbsCurve k; if (CurveFromON(ac, k)) { SceneObject n = SceneObject::MakeCurve(k); n.layer_index = layer; ctx.Doc().Add(std::move(n)); } }
        return;
      }
    }
    for (double t : c.SuggestedParameterValues(0.005)) {
      Vector3d tan = c.TangentAt(t); Vector3d s = ON_CrossProduct(tan, up); s.Unitize();
      pts.push_back(c.PointAt(t) + s * d);
    }
    if (pts.size() < 2) return;
    SceneObject n = SceneObject::MakeCurve(c.Degree() == 1 ? PolylineCurve(pts) : kernel::NurbsCurve::FromControlPoints(pts, std::min(3, static_cast<int>(pts.size()) - 1)));
    n.layer_index = layer;
    ctx.Doc().Add(std::move(n));
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      const int sign = SideSign(ctx, *o->curve, h);
      std::vector<Point3d> pts;
      Vector3d up = ActiveNormal(ctx);
      for (double t : o->curve->SuggestedParameterValues(0.02)) { Vector3d tan = o->curve->TangentAt(t); Vector3d s = ON_CrossProduct(tan, up); s.Unitize(); pts.push_back(o->curve->PointAt(t) + s * (distance_ * sign)); }
      ctx.AddPreviewPolyline(pts, o->curve->IsClosed());
    }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  double distance_ = 1.0;
  bool both_ = false;
};

// Prompts for a name and applies it to every selected object (a single
// object gets the name as typed; more than one gets it suffixed "(2)",
// "(3)", ... so names stay distinct).
class SetObjectNameCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to name"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (ids.empty()) { ctx.Warn("Nothing selected"); Finish(); return; }
    ids_ = ids;
    WantText("Name");
  }
  void OnText(CommandContext& ctx, const std::string& name) override {
    if (name.empty()) { ctx.Warn("SetObjectName: name cannot be empty"); Finish(); return; }
    ctx.Doc().BeginChange("SetObjectName");
    int i = 0;
    for (ObjectId id : ids_) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      ++i;
      o->name = ids_.size() > 1 ? name + " (" + std::to_string(i) + ")" : name;
    }
    ctx.Print("SetObjectName: named " + std::to_string(ids_.size()) + " object(s) '" + name + "'" + (ids_.size() > 1 ? " (2), (3), ..." : ""));
    Finish();
  }
  std::vector<ObjectId> ids_;
};

// Prompts for a key then a value and sets that user-text pair on every
// selected object (Rhino's Properties > Attribute User Text field, scripted).
class SetUserTextCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (ids.empty()) { ctx.Warn("Nothing selected"); Finish(); return; }
    ids_ = ids;
    WantText("User text key");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (!have_key_) {
      if (t.empty()) { ctx.Warn("SetUserText: key cannot be empty"); Finish(); return; }
      key_ = t;
      have_key_ = true;
      WantText("Value for '" + key_ + "'");
      return;
    }
    ctx.Doc().BeginChange("SetUserText");
    for (ObjectId id : ids_) if (SceneObject* o = ctx.Doc().Find(id)) o->user_text[key_] = t;
    ctx.Print("SetUserText: " + key_ + " = " + t + " on " + std::to_string(ids_.size()) + " object(s)");
    Finish();
  }
  std::vector<ObjectId> ids_;
  std::string key_;
  bool have_key_ = false;
};

// Extend the nearer end of each selected curve towards a picked point (Enter
// falls back to a fixed 10% of the domain at both ends).
class ExtendCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to extend"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) ids_.push_back(id); }
    if (ids_.empty()) { ctx.Warn("Select curves"); Finish(); return; }
    WantPoint("Point to extend towards (Enter for 10% of the domain at both ends)");
  }
  void OnPoint(CommandContext& ctx, Point3d target) override {
    ctx.Doc().BeginChange("Extend");
    int done = 0;
    for (ObjectId id : ids_) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      const kernel::Interval d = o->curve->Domain();
      const Point3d startp = o->curve->PointAt(d.min), endp = o->curve->PointAt(d.max);
      const bool near_start = target.DistanceTo(startp) <= target.DistanceTo(endp);
      const Point3d endpoint = near_start ? startp : endp;
      Vector3d tan = o->curve->TangentAt(near_start ? d.min : d.max);
      if (near_start) tan = -tan;  // outward direction of travel at the start
      if (!tan.Unitize()) continue;
      const double len = ON_DotProduct(target - endpoint, tan);
      if (len <= 1e-9) continue;
      const double curve_len = o->curve->Length();
      const double domain_len = d.max - d.min;
      const double param_delta = curve_len > 1e-9 ? domain_len * (len / curve_len) : 0;
      if (param_delta <= 0) continue;
      const kernel::Result r = near_start ? o->curve->Extend(d.min - param_delta, d.max) : o->curve->Extend(d.min, d.max + param_delta);
      if (r != kernel::Result::Failed) { o->InvalidateDisplay(); ++done; }
    }
    ctx.Print("Extend: " + std::to_string(done) + " curve(s) extended towards the picked point");
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    ctx.Doc().BeginChange("Extend");
    int done = 0;
    for (ObjectId id : ids_) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      const kernel::Interval d = o->curve->Domain();
      const double len = d.max - d.min;
      const kernel::Result r = o->curve->Extend(d.min - len * 0.1, d.max + len * 0.1);
      if (r != kernel::Result::Failed) { o->InvalidateDisplay(); ++done; }
    }
    ctx.Print("Extend: " + std::to_string(done) + " curve(s) extended by 10% of their domain at both ends");
    Finish();
  }
  void OnCancel(CommandContext&) override {}
  std::vector<ObjectId> ids_;
};

// Picks the single control point of a curve/surface selection nearest a
// clicked point, then sets its weight (making the object rational if it
// wasn't already). With no pick (Enter), falls back to making every
// selected object rational at weight 1 - equivalent to the old behaviour.
class WeightCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves or surfaces"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantPoint("Pick a control point to weight (Enter to make every object rational at weight 1)");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ObjectId best_id = kNoObject;
    bool best_is_curve = true;
    int best_i = -1, best_j = -1;
    double best_d = std::numeric_limits<double>::max();
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (o->kind == ObjectKind::Curve) {
        for (int i = 0; i < o->curve->ControlPointCount(); ++i) {
          const double d = o->curve->ControlPointAt(i).DistanceTo(p);
          if (d < best_d) { best_d = d; best_id = id; best_is_curve = true; best_i = i; best_j = -1; }
        }
      } else if (o->kind == ObjectKind::Surface) {
        for (int i = 0; i < o->surface->CVCountU(); ++i)
          for (int j = 0; j < o->surface->CVCountV(); ++j) {
            const double d = o->surface->ControlPointAt(i, j).DistanceTo(p);
            if (d < best_d) { best_d = d; best_id = id; best_is_curve = false; best_i = i; best_j = j; }
          }
      }
    }
    if (best_id == kNoObject) { ctx.Warn("Weight: no control points found"); Finish(); return; }
    id_ = best_id;
    is_curve_ = best_is_curve;
    i_ = best_i;
    j_ = best_j;
    SceneObject* o = ctx.Doc().Find(id_);
    const double cur = is_curve_ ? o->curve->WeightAt(i_) : o->surface->WeightAt(i_, j_);
    WantNumber("Weight for this control point", cur);
  }
  void OnNumber(CommandContext& ctx, double w) override {
    if (w <= 0) { ctx.Warn("Weight: must be positive"); Finish(); return; }
    SceneObject* o = ctx.Doc().Find(id_);
    if (!o) { Finish(); return; }
    ctx.Doc().BeginChange("Weight");
    const kernel::Result r = is_curve_ ? o->curve->SetWeightAt(i_, w) : o->surface->SetWeightAt(i_, j_, w);
    if (r != kernel::Result::Failed) { o->InvalidateDisplay(); ctx.Print("Weight: control point set to " + FormatNumber(w)); }
    else ctx.Warn("Weight: could not set that control point's weight");
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    ctx.Doc().BeginChange("Weight");
    int done = 0;
    for (ObjectId id : ids_) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (o->kind == ObjectKind::Curve && o->curve->MakeRational() != kernel::Result::Failed) { o->InvalidateDisplay(); ++done; }
      else if (o->kind == ObjectKind::Surface && o->surface->MakeRational() != kernel::Result::Failed) { o->InvalidateDisplay(); ++done; }
    }
    ctx.Print("Weight: " + std::to_string(done) + " object(s) made rational at weight 1");
    Finish();
  }
  std::vector<ObjectId> ids_;
  ObjectId id_ = kNoObject;
  bool is_curve_ = true;
  int i_ = -1, j_ = -1;
};

// Inserts a knot at the closest point on each selected curve/surface to a
// picked point (Enter falls back to the domain midpoint of each object).
class InsertKnotCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves or surfaces"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantPoint("Point on the curve/surface to insert a knot at (Enter for the domain midpoint)");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.Doc().BeginChange("InsertKnot");
    int done = 0;
    for (ObjectId id : ids_) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (o->kind == ObjectKind::Curve) {
        const double t = o->curve->ClosestPointParameter(p);
        if (o->curve->InsertKnotAt(t) != kernel::Result::Failed) { o->InvalidateDisplay(); ++done; }
      } else if (o->kind == ObjectKind::Surface) {
        const kernel::Point2d uv = o->surface->ClosestPointParameter(p);
        bool changed = false;
        if (o->surface->InsertKnotAt(0, uv.x) != kernel::Result::Failed) changed = true;
        if (o->surface->InsertKnotAt(1, uv.y) != kernel::Result::Failed) changed = true;
        if (changed) { o->InvalidateDisplay(); ++done; }
      }
    }
    ctx.Print("InsertKnot: " + std::to_string(done) + " object(s) got a knot at the picked point");
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    ctx.Doc().BeginChange("InsertKnot");
    int done = 0;
    for (ObjectId id : ids_) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (o->kind == ObjectKind::Curve) {
        const kernel::Interval d = o->curve->Domain();
        if (o->curve->InsertKnotAt((d.min + d.max) / 2) != kernel::Result::Failed) { o->InvalidateDisplay(); ++done; }
      } else if (o->kind == ObjectKind::Surface) {
        const kernel::Interval d = o->surface->Domain(0);
        if (o->surface->InsertKnotAt(0, (d.min + d.max) / 2) != kernel::Result::Failed) { o->InvalidateDisplay(); ++done; }
      }
    }
    ctx.Print("InsertKnot: " + std::to_string(done) + " object(s) got a knot at the domain midpoint");
    Finish();
  }
  std::vector<ObjectId> ids_;
};

}  // namespace

void RegisterEditCommands(CommandEngine& e) {
  Reg(e, "Delete", OnSelection("Select objects to delete", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Delete");
        for (ObjectId id : ids) ctx.Doc().Remove(id);
        std::vector<int> lights;
        for (const Light& l : ctx.Doc().Lights()) if (l.selected) lights.push_back(l.id);
        for (int id : lights) ctx.Doc().RemoveLight(id);
        ctx.Print("Deleted " + std::to_string(ids.size()) + " object(s)" + (lights.empty() ? "" : " and " + std::to_string(lights.size()) + " light(s)"));
      }));
  Reg(e, "Undo", Immediate([](CommandContext& ctx) { if (!ctx.Doc().Undo()) ctx.Print("Nothing to undo"); }));
  Reg(e, "Redo", Immediate([](CommandContext& ctx) { if (!ctx.Doc().Redo()) ctx.Print("Nothing to redo"); }));
  Reg(e, "UndoMultiple", Immediate([](CommandContext& ctx) { ctx.App().Panels().undo_multiple = true; }));
  Reg(e, "RedoMultiple", Immediate([](CommandContext& ctx) { ctx.App().Panels().redo_multiple = true; }));
  Reg(e, "ClearUndo", Immediate([](CommandContext& ctx) { ctx.Doc().ClearUndo(); ctx.Print("Undo history cleared"); }));
  Reg(e, "Group", OnSelection("Select objects to group", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Group");
        int g = ctx.Doc().CreateGroup(ids);
        ctx.Print("Group " + std::to_string(g) + " created with " + std::to_string(ids.size()) + " object(s)");
      }, 1));
  Reg(e, "Ungroup", OnSelection("Select groups to ungroup", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ctx.Doc().BeginChange("Ungroup"); ctx.Doc().Ungroup(ids); }));
  Reg(e, "AddToGroup", OnSelection("Select objects to add to the selected group", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        int g = -1;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->group_id >= 0) { g = o->group_id; break; }
        if (g < 0) { ctx.Warn("Selection contains no group"); return; }
        ctx.Doc().BeginChange("AddToGroup");
        for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) o->group_id = g;
      }));
  Reg(e, "RemoveFromGroup", OnSelection("Select objects to remove from their group", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ctx.Doc().BeginChange("RemoveFromGroup"); ctx.Doc().Ungroup(ids); }));
  Reg(e, "Hide", OnSelection("Select objects to hide", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { HideShow(ctx, ids, false, "Hide"); }));
  Reg(e, "Show", Immediate([](CommandContext& ctx) {
        ctx.Doc().BeginChange("Show");
        int n = 0;
        for (SceneObject& o : ctx.Doc().Objects()) if (!o.visible) { o.visible = true; ++n; }
        ctx.Print("Showed " + std::to_string(n) + " object(s)");
      }));
  Reg(e, "ShowSelected", Immediate([](CommandContext& ctx) {
        ctx.Doc().BeginChange("ShowSelected");
        for (SceneObject& o : ctx.Doc().Objects()) if (!o.visible) { o.visible = true; o.selected = true; }
      }), CommandStatus::Implemented, "Shows every hidden object and selects it.");
  Reg(e, "HideSwap", Immediate([](CommandContext& ctx) { ctx.Doc().BeginChange("HideSwap"); for (SceneObject& o : ctx.Doc().Objects()) { o.visible = !o.visible; o.selected = false; } }));
  Reg(e, "Isolate", OnSelection("Select objects to isolate", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Isolate");
        for (SceneObject& o : ctx.Doc().Objects()) o.visible = std::find(ids.begin(), ids.end(), o.id) != ids.end();
      }));
  Reg(e, "Unisolate", Immediate([](CommandContext& ctx) { ctx.Doc().BeginChange("Unisolate"); for (SceneObject& o : ctx.Doc().Objects()) o.visible = true; }));
  Reg(e, "Lock", OnSelection("Select objects to lock", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ctx.Doc().BeginChange("Lock"); for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->locked = true; o->selected = false; } }));
  Reg(e, "Unlock", Immediate([](CommandContext& ctx) { ctx.Doc().BeginChange("Unlock"); for (SceneObject& o : ctx.Doc().Objects()) o.locked = false; }));
  Reg(e, "UnlockSelected", Immediate([](CommandContext& ctx) { ctx.Doc().BeginChange("UnlockSelected"); for (SceneObject& o : ctx.Doc().Objects()) if (o.locked) { o.locked = false; o.selected = true; } }));
  Reg(e, "LockSwap", Immediate([](CommandContext& ctx) { ctx.Doc().BeginChange("LockSwap"); for (SceneObject& o : ctx.Doc().Objects()) { o.locked = !o.locked; o.selected = false; } }));
  Reg(e, "Join", OnSelection("Select objects to join", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        // Curves: chain end-to-end into one polycurve (NURBS form). Surfaces/breps: append into one polysurface. Meshes: merge.
        // Copies, not pointers: Add()/Remove() below reallocate the object vector.
        std::vector<SceneObject> curves, breps, meshes;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          if (o->kind == ObjectKind::Curve) curves.push_back(*o);
          else if (o->kind == ObjectKind::Brep || o->kind == ObjectKind::Surface) breps.push_back(*o);
          else if (o->kind == ObjectKind::Mesh) meshes.push_back(*o);
        }
        ctx.Doc().BeginChange("Join");
        if (curves.size() >= 2) {
          ON_PolyCurve pc;
          std::vector<const SceneObject*> remaining;
          for (const SceneObject& c : curves) remaining.push_back(&c);
          pc.Append(new ON_NurbsCurve(remaining[0]->curve->raw()));
          std::vector<ObjectId> joined_ids = {remaining[0]->id};
          remaining.erase(remaining.begin());
          const double tol = ctx.Settings().absolute_tolerance * 10;
          bool progress = true;
          while (progress && !remaining.empty()) {
            progress = false;
            for (size_t i = 0; i < remaining.size(); ++i) {
              ON_NurbsCurve c = remaining[i]->curve->raw();
              if (c.PointAtStart().DistanceTo(pc.PointAtEnd()) <= tol) { pc.Append(new ON_NurbsCurve(c)); }
              else if (c.PointAtEnd().DistanceTo(pc.PointAtEnd()) <= tol) { c.Reverse(); pc.Append(new ON_NurbsCurve(c)); }
              else if (c.PointAtEnd().DistanceTo(pc.PointAtStart()) <= tol) { pc.Prepend(new ON_NurbsCurve(c)); }
              else if (c.PointAtStart().DistanceTo(pc.PointAtStart()) <= tol) { c.Reverse(); pc.Prepend(new ON_NurbsCurve(c)); }
              else continue;
              joined_ids.push_back(remaining[i]->id);
              remaining.erase(remaining.begin() + static_cast<long>(i));
              progress = true;
              break;
            }
          }
          if (pc.Count() >= 2) {
            kernel::NurbsCurve k;
            if (CurveFromON(pc, k)) {
              SceneObject n = SceneObject::MakeCurve(k);
              n.layer_index = curves[0].layer_index;
              for (ObjectId jid : joined_ids) ctx.Doc().Remove(jid);
              ctx.Doc().Add(std::move(n));
              ctx.Print("Joined " + std::to_string(pc.Count()) + " curves into one");
            }
          } else ctx.Warn("Curve ends do not meet");
        }
        if (breps.size() >= 2) {
          ON_Brep* b = new ON_Brep();
          for (const SceneObject& o : breps) {
            if (o.kind == ObjectKind::Brep) b->Append(o.brep->raw());
            else { ON_Brep tmp; ON_NurbsSurface* srf = new ON_NurbsSurface(o.surface->raw()); tmp.Create(srf); b->Append(tmp); }
          }
          JoinNakedEdges(*b, ctx.Settings().absolute_tolerance * 10);
          kernel::Brep k; k.raw() = *b; delete b;
          SceneObject n = SceneObject::MakeBrep(k);
          n.layer_index = breps[0].layer_index;
          for (const SceneObject& o : breps) ctx.Doc().Remove(o.id);
          ctx.Doc().Add(std::move(n));
          ctx.Print("Joined " + std::to_string(breps.size()) + " surfaces into one polysurface");
        }
        if (meshes.size() >= 2) {
          std::vector<kernel::Mesh> ms;
          for (const SceneObject& o : meshes) ms.push_back(*o.mesh);
          SceneObject n = SceneObject::MakeMesh(kernel::Mesh::MergeAndWeld(ms, ctx.Settings().absolute_tolerance));
          n.layer_index = meshes[0].layer_index;
          for (const SceneObject& o : meshes) ctx.Doc().Remove(o.id);
          ctx.Doc().Add(std::move(n));
          ctx.Print("Joined " + std::to_string(meshes.size()) + " meshes");
        }
      }, 2));
  Reg(e, "Explode", OnSelection("Select objects to explode", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Explode");
        int made = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          const int layer = o->layer_index;
          if (o->kind == ObjectKind::Brep) {
            const ON_Brep b = o->brep->raw();  // copy: Add() may reallocate objects
            for (int f = 0; f < b.m_F.Count(); ++f) {
              ON_Brep* face = b.DuplicateFace(f, true);
              if (!face) continue;
              kernel::Brep k; k.raw() = *face; delete face;
              SceneObject n = SceneObject::MakeBrep(k); n.layer_index = layer; ctx.Doc().Add(std::move(n)); ++made;
            }
            ctx.Doc().Remove(id);
          } else if (o->kind == ObjectKind::Curve) {
            // Split at kinks / polyline vertices.
            const ON_NurbsCurve c = o->curve->raw();  // copy: Add() may reallocate objects
            ON_SimpleArray<double> kinks;
            const int span = c.SpanCount();
            ON_SimpleArray<double> knots(span + 1);
            knots.SetCount(span + 1);
            c.GetSpanVector(knots.Array());
            std::vector<double> splits;
            for (int i = 1; i < span; ++i) {
              ON_3dVector t0 = c.TangentAt(knots[i] - 1e-6), t1 = c.TangentAt(knots[i] + 1e-6);
              if (ON_DotProduct(t0, t1) < std::cos(1.0 * ON_PI / 180.0)) splits.push_back(knots[i]);
            }
            if (splits.empty()) continue;
            double t0 = c.Domain().Min();
            splits.push_back(c.Domain().Max());
            for (double t1 : splits) {
              ON_NurbsCurve seg = c;
              if (seg.Trim(ON_Interval(t0, t1))) { kernel::NurbsCurve k; k.raw() = seg; SceneObject n = SceneObject::MakeCurve(k); n.layer_index = layer; ctx.Doc().Add(std::move(n)); ++made; }
              t0 = t1;
            }
            ctx.Doc().Remove(id);
          } else if (o->kind == ObjectKind::Mesh) {
            const std::vector<kernel::Mesh> parts = kernel::Decompose(*o->mesh);
            for (const kernel::Mesh& part : parts) { SceneObject n = SceneObject::MakeMesh(part); n.layer_index = layer; ctx.Doc().Add(std::move(n)); ++made; }
            ctx.Doc().Remove(id);
          }
        }
        ctx.Print("Exploded into " + std::to_string(made) + " object(s)");
      }));
  Reg(e, "Rebuild", Make<RebuildCommand>());
  Reg(e, "ChangeDegree", Make<ChangeDegreeCommand>(), CommandStatus::Implemented,
      "Elevates curves/surfaces to a typed target degree (elevation only - it never lowers a degree, which is a lossy refit, not a plain elevation).");
  Reg(e, "Flip", OnSelection("Select curves, surfaces or meshes to flip", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Flip");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (!o) continue; if (o->kind == ObjectKind::Curve) o->curve->Reverse(); else if (o->kind == ObjectKind::Surface) o->surface->Reverse(0); else if (o->kind == ObjectKind::Mesh) *o->mesh = o->mesh->FlipNormals(); else if (o->kind == ObjectKind::Brep) o->brep->raw().Flip(); o->InvalidateDisplay(); }
      }));
  Reg(e, "Dir", OnSelection("Select objects to show direction", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o) continue; if (o->kind == ObjectKind::Curve) { kernel::Interval d = o->curve->Domain(); ctx.Print("Curve " + std::to_string(id) + ": start " + FormatPoint(o->curve->PointAt(d.min)) + " tangent " + FormatPoint(Point3d(o->curve->TangentAt(d.min)))); } else if (o->kind == ObjectKind::Surface) { ctx.Print("Surface " + std::to_string(id) + ": normal at centre " + FormatPoint(Point3d(o->surface->NormalAt((o->surface->Domain(0).min + o->surface->Domain(0).max) / 2, (o->surface->Domain(1).min + o->surface->Domain(1).max) / 2)))); } }
        ctx.Print("Use Flip to reverse direction.");
      }), CommandStatus::Partial,
      "Reports each curve's start point/tangent or surface's centre normal in the command history; there are no clickable direction-arrow glyphs drawn in the viewport, so reversing is a separate Flip call rather than a click on the arrow itself.");
  Reg(e, "Offset", Make<OffsetCommand>());
  Reg(e, "Extend", Make<ExtendCommand>(), CommandStatus::Implemented,
      "Extends the end of each curve nearer the picked point until its tangent line reaches that point (Enter extends both ends by a fixed 10% of the domain instead).");
  Reg(e, "MakePeriodic", OnSelection("Select curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        // A closed clamped curve becomes a periodic uniform curve through the
        // same control points (like Rhino's Smooth=Yes: the shape relaxes
        // slightly but the seam becomes smooth). Only re-knotting the clamped
        // CVs, as before, tore the curve open at the seam.
        ctx.Doc().BeginChange("MakePeriodic");
        int made = 0;
        for (ObjectId id : ids) {
          SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Curve) continue;
          ON_NurbsCurve& c = o->curve->raw();
          if (c.IsPeriodic()) { ++made; continue; }
          const int order = c.Order();
          int n = c.CVCount();
          if (c.IsClosed() && n > order) --n;  // the duplicated seam CV
          if (n < order) continue;
          std::vector<ON_3dPoint> cvs;
          for (int i = 0; i < n; ++i) { ON_3dPoint p; c.GetCV(i, p); cvs.push_back(p); }
          ON_NurbsCurve periodic;
          if (periodic.CreatePeriodicUniformNurbs(3, order, n, cvs.data())) { c = periodic; o->InvalidateDisplay(); ++made; }
        }
        ctx.Print("MakePeriodic: " + std::to_string(made) + " curve(s) made periodic");
      }), CommandStatus::Partial,
      "Only the Smooth=Yes behaviour is implemented (a periodic-uniform curve refit through the same control points, seam relaxed smooth); Smooth=No's exact-shape-preserving re-knot is a distinct, considerably harder NURBS algorithm this build does not have.");
  Reg(e, "Weight", Make<WeightCommand>(), CommandStatus::Implemented,
      "Picks the nearest control point to a clicked point and sets its weight (making the curve/surface rational if needed); Enter instead makes every selected object rational at weight 1.");
  Reg(e, "PointsOn", OnSelection("Select objects to turn on control points", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->show_control_points = true; o->InvalidateDisplay(); } ctx.Print("Control points on for " + std::to_string(ids.size()) + " object(s)"); }));
  Reg(e, "PointsOff", Immediate([](CommandContext& ctx) { int n = 0; for (SceneObject& o : ctx.Doc().Objects()) if (o.show_control_points) { o.show_control_points = false; o.InvalidateDisplay(); ++n; } ctx.Print("Control points off (" + std::to_string(n) + " object(s))"); }));
  Reg(e, "SolidPtOn", OnSelection("Select polysurfaces", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->show_control_points = true; o->InvalidateDisplay(); } }), CommandStatus::Implemented, "Turns on control points for the selected polysurfaces (same toggle as PointsOn).");
  Reg(e, "InsertKnot", Make<InsertKnotCommand>(), CommandStatus::Implemented,
      "Inserts a knot at the closest point to the pick (both directions on a surface); Enter falls back to the domain midpoint.");
  Reg(e, "SetObjectName", Make<SetObjectNameCommand>(), CommandStatus::Implemented,
      "Prompts for a name and applies it to every selected object, suffixed \"(2)\", \"(3)\", ... when more than one is selected.");
  Reg(e, "SetUserText", Make<SetUserTextCommand>(), CommandStatus::Implemented, "Prompts for a key then a value and sets that user-text pair on every selected object.");
  Reg(e, "GetUserText", OnSelection("Select objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) for (const auto& [k, v] : o->user_text) ctx.Print(std::to_string(id) + ": " + k + " = " + v); }));
  Reg(e, "DocumentUserText", Immediate([](CommandContext& ctx) { ctx.App().Panels().document_user_text = true; }));
  Reg(e, "GetDocumentUserText", Immediate([](CommandContext& ctx) { for (const auto& [k, v] : ctx.Doc().UserText()) ctx.Print(k + " = " + v); }));
  Reg(e, "BoxEdit", Immediate([](CommandContext& ctx) { ctx.App().Panels().box_edit = true; }));
  Reg(e, "Properties", Immediate([](CommandContext& ctx) { ctx.App().Panels().properties = true; }));
  Reg(e, "ObjectProperties", Immediate([](CommandContext& ctx) { ctx.App().Panels().properties = true; }));
  Reg(e, "CopyToClipboard", OnSelection("Select objects to copy", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::string path = (std::filesystem::temp_directory_path() / "dino8_clipboard.3dm").string();
        Document sub;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) sub.Add(*o);
        sub.Layers() = ctx.Doc().Layers();
        std::string err;
        if (Save3dm(sub, path, err)) ctx.Print("Copied " + std::to_string(ids.size()) + " object(s) to the clipboard"); else ctx.Warn(err);
      }));
  Reg(e, "Cut", OnSelection("Select objects to cut", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::string path = (std::filesystem::temp_directory_path() / "dino8_clipboard.3dm").string();
        Document sub;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) sub.Add(*o);
        sub.Layers() = ctx.Doc().Layers();
        std::string err;
        if (!Save3dm(sub, path, err)) { ctx.Warn(err); return; }
        ctx.Doc().BeginChange("Cut");
        for (ObjectId id : ids) ctx.Doc().Remove(id);
      }));
  Reg(e, "Paste", Immediate([](CommandContext& ctx) {
        std::string path = (std::filesystem::temp_directory_path() / "dino8_clipboard.3dm").string();
        std::string err;
        if (!ctx.App().ImportFile(path, err)) ctx.Warn("Clipboard is empty");
      }));
}

}  // namespace dino8::app
