// Extended selection commands: Select by name/id, duplicates, fence /
// circular / volume picks, annotation and block selection, user-text
// matching, sub-object selection filters, group naming.
#include "commands/cmd_common.h"

#include <cctype>
#include <map>
#include <set>

#include "doc/SubObjectEdit.h"

namespace dino8::app {

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

void Report(CommandContext& ctx) { ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) selected"); }

bool Selectable(CommandContext& ctx, const SceneObject& o) { return ctx.Doc().IsObjectVisible(o) && !ctx.Doc().IsObjectLocked(o); }

CommandFactory SelWhere(std::function<bool(CommandContext&, const SceneObject&)> pred) {
  return Immediate([pred](CommandContext& ctx) {
    ctx.Doc().SelectWhere([&](const SceneObject& o) { return Selectable(ctx, o) && pred(ctx, o); }, true);
    Report(ctx);
  });
}

// Objects that belong to a group whose name is one of `labels` (annotation
// and hatch commands group their curves under the command name).
CommandFactory SelGroupNamed(std::vector<std::string> labels) {
  return Immediate([labels](CommandContext& ctx) {
    std::set<int> groups;
    for (const Group& g : ctx.Doc().Groups())
      for (const std::string& l : labels) if (Lower(g.name) == Lower(l)) groups.insert(g.id);
    ctx.Doc().SelectWhere([&](const SceneObject& o) { return Selectable(ctx, o) && groups.count(o.group_id) > 0; }, true);
    Report(ctx);
  });
}

CommandFactory NoSuchObjects(const char* what) {
  return Immediate([what](CommandContext& ctx) { ctx.Print(std::string("0 objects selected (no ") + what + " in this document)"); });
}

CommandFactory SubObjectPlanned(const char* what) {
  return Immediate([what](CommandContext& ctx) { ctx.Print(std::string(what) + ": sub-object selection is coming; select whole objects for now."); });
}

// ---------------------------------------------------------------------------
// Sub-object selection helpers shared by the Sel* control-point / edge / face
// commands below. All of them work on top of Application::SubSelection() -
// the set the viewport fills when the user Ctrl+Shift-clicks a sub-object,
// or clicks a control point with PointsOn.
// ---------------------------------------------------------------------------

SubObjectSelection& Sub(CommandContext& ctx) { return ctx.App().SubSelection(); }

// Nearest control point (of any selected object with PointsOn) to a picked
// point; the object it belongs to (for the U/V grid lookup).
std::optional<SubObjectRef> NearestControlPoint(CommandContext& ctx, Point3d p, ObjectId* owner = nullptr) {
  std::optional<SubObjectRef> best;
  double bd = std::numeric_limits<double>::max();
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!o.selected && !o.show_control_points) continue;
    if (!o.show_control_points) continue;
    const int n = ControlPointCount(o);
    for (int i = 0; i < n; ++i) {
      Point3d cp;
      if (!ControlPointPosition(o, i, cp)) continue;
      const double d = cp.DistanceTo(p);
      if (d < bd) { bd = d; best = SubObjectRef::Vertex(o.id, i); if (owner) *owner = o.id; }
    }
  }
  return best;
}

// Sub-objects the current selection already carries, restricted to one kind.
std::vector<SubObjectRef> SelectedOfKind(CommandContext& ctx, SubObjectKind k) {
  std::vector<SubObjectRef> out;
  for (const SubObjectRef& r : Sub(ctx).Items()) if (r.kind == k) out.push_back(r);
  return out;
}

void ReportSub(CommandContext& ctx, const char* what, size_t added) {
  ctx.Print(std::string(what) + ": " + std::to_string(added) + " sub-object(s) added, " + std::to_string(Sub(ctx).Size()) + " selected");
}

// A command that asks the user to click near a control point / edge, then
// runs `apply` on the picked ref (repeatable, Enter to finish).
class PickSubObjectCommand : public Command {
 public:
  using NearestFn = std::function<std::optional<SubObjectRef>(CommandContext&, Point3d)>;
  using Apply = std::function<void(CommandContext&, const SubObjectRef&)>;
  PickSubObjectCommand(std::string prompt, NearestFn nearest, Apply apply)
      : prompt_(std::move(prompt)), nearest_(std::move(nearest)), apply_(std::move(apply)) {}
  void Begin(CommandContext&) override { WantPoint(prompt_); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    std::optional<SubObjectRef> r = nearest_(ctx, p);
    if (!r) { ctx.Warn("Nothing found near that point"); return; }
    apply_(ctx, *r);
    Finish();
  }

 private:
  std::string prompt_;
  NearestFn nearest_;
  Apply apply_;
};

// SoftEditSrf / SoftEditCrv: click a control point, give a falloff radius
// and a move vector; every control point of the same object within the
// radius moves by a fraction of the vector (smoothstep falloff), so the
// picked point moves fully and the edit fades out toward the radius.
class SoftEditCommand : public Command {
 public:
  enum class Kind { Surface, Curve };
  explicit SoftEditCommand(Kind kind) : kind_(kind) {}
  void Begin(CommandContext&) override { WantPoint("Click near a control point to soft-edit"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ObjectId owner = kNoObject;
    std::optional<SubObjectRef> r = NearestControlPoint(ctx, p, &owner);
    const SceneObject* o = r ? ctx.Doc().Find(r->id) : nullptr;
    const bool kind_ok = o && ((kind_ == Kind::Surface && o->kind == ObjectKind::Surface) || (kind_ == Kind::Curve && o->kind == ObjectKind::Curve));
    if (!r || !kind_ok) { ctx.Warn("No matching control point near that point (turn PointsOn first)"); return; }
    base_ = *r;
    WantNumber("Falloff radius", 1.0);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!base_) return;
    if (!radius_) { radius_ = std::max(v, 1e-6); WantText("Move by dx,dy,dz"); return; }
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (!base_ || !radius_) return;
    double dx = 0, dy = 0, dz = 0;
    std::sscanf(t.c_str(), "%lf,%lf,%lf", &dx, &dy, &dz);
    Apply(ctx, Vector3d(dx, dy, dz));
    Finish();
  }

 private:
  void Apply(CommandContext& ctx, Vector3d delta) {
    SceneObject* o = ctx.Doc().Find(base_->id);
    if (!o) return;
    Point3d base_p;
    if (!ControlPointPosition(*o, base_->index, base_p)) return;
    // Only base_->id's control points move; a single known existing
    // object, nothing else about the document changes - fast path.
    ctx.Doc().BeginChangeForObjects("SoftEdit", {base_->id});
    const int n = ControlPointCount(*o);
    int moved = 0;
    for (int i = 0; i < n; ++i) {
      Point3d p;
      if (!ControlPointPosition(*o, i, p)) continue;
      const double dist = p.DistanceTo(base_p);
      if (dist > *radius_) continue;
      const double t = 1.0 - dist / *radius_;
      const double w = t * t * (3 - 2 * t);  // smoothstep falloff
      SetControlPointPosition(*o, i, p + delta * w);
      ++moved;
    }
    o->InvalidateDisplay();
    ctx.Print(std::string(kind_ == Kind::Surface ? "SoftEditSrf" : "SoftEditCrv") + ": " + std::to_string(moved) +
               " control point(s) moved within radius " + FormatNumber(*radius_));
  }
  Kind kind_;
  std::optional<SubObjectRef> base_;
  std::optional<double> radius_;
};

// A command driven by SelectThenAct semantics but that needs the picked
// control point too - used by SelU/SelV/SelUV: click a control point,
// the row/column through it is selected.
CommandFactory SelControlPointGrid(const char* label, bool along_u, bool along_v) {
  return [=]() -> std::unique_ptr<Command> {
    return std::make_unique<PickSubObjectCommand>(
        std::string("Click a control point (") + label + ")",
        [](CommandContext& ctx, Point3d p) { return NearestControlPoint(ctx, p); },
        [along_u, along_v, label](CommandContext& ctx, const SubObjectRef& r) {
          const SceneObject* o = ctx.Doc().Find(r.id);
          size_t added = 0;
          if (o && o->kind == ObjectKind::Surface) {
            int nu, nv;
            SurfaceGrid(*o, nu, nv);
            const int i = r.index / nv, j = r.index % nv;
            if (along_u) for (int jj = 0; jj < nv; ++jj) { SubObjectRef rr = SubObjectRef::Vertex(r.id, i * nv + jj); if (!Sub(ctx).Contains(rr)) ++added; Sub(ctx).Add(rr); }
            if (along_v) for (int ii = 0; ii < nu; ++ii) { SubObjectRef rr = SubObjectRef::Vertex(r.id, ii * nv + j); if (!Sub(ctx).Contains(rr)) ++added; Sub(ctx).Add(rr); }
          } else {
            if (!Sub(ctx).Contains(r)) ++added;
            Sub(ctx).Add(r);
          }
          ReportSub(ctx, label, added);
        });
  };
}

Point3d Center(const SceneObject& o) { kernel::BoundingBox b = o.BoundingBox(); return (b.min + b.max) * 0.5; }

bool BoxesTouch(const kernel::BoundingBox& a, const kernel::BoundingBox& b, double tol) {
  return a.min.x <= b.max.x + tol && b.min.x <= a.max.x + tol && a.min.y <= b.max.y + tol && b.min.y <= a.max.y + tol &&
         a.min.z <= b.max.z + tol && b.min.z <= a.max.z + tol;
}

int PointCount(const SceneObject& o) {
  switch (o.kind) {
    case ObjectKind::Point: return 1;
    case ObjectKind::Curve: return o.curve->ControlPointCount();
    case ObjectKind::Surface: return o.surface->CVCountU() * o.surface->CVCountV();
    case ObjectKind::Brep: return o.brep->raw().m_V.Count() * 1000 + o.brep->raw().m_F.Count();
    case ObjectKind::Mesh: return o.mesh->VertexCount() * 1000 + o.mesh->FaceCount();
    case ObjectKind::SubD: return o.subd->raw().VertexCount() * 1000 + o.subd->raw().FaceCount();
  }
  return 0;
}

// Moller-Trumbore ray/triangle test (DrapePt). Small and self-contained here
// rather than shared with cmd_surface.cpp's own copy (used by Project/Pull),
// which is private to that file's anonymous namespace.
bool RayTriangleHit(Point3d o, Vector3d d, Point3d a, Point3d b, Point3d c, double& t) {
  const Vector3d e1 = b - a, e2 = c - a;
  const Vector3d p = ON_CrossProduct(d, e2);
  const double det = ON_DotProduct(e1, p);
  if (std::fabs(det) < 1e-12) return false;
  const double inv = 1 / det;
  const Vector3d s = o - a;
  const double u = ON_DotProduct(s, p) * inv;
  if (u < -1e-9 || u > 1 + 1e-9) return false;
  const Vector3d q = ON_CrossProduct(s, e1);
  const double v = ON_DotProduct(d, q) * inv;
  if (v < -1e-9 || u + v > 1 + 1e-9) return false;
  t = ON_DotProduct(e2, q) * inv;
  return true;
}

// Nearest intersection of the ray p + t*d (t >= 0 only - DrapePt drops
// points straight down onto whatever is below them) with the mesh.
std::optional<Point3d> RayHitMesh(const kernel::Mesh& m, Point3d p, Vector3d d) {
  const ON_Mesh& r = m.raw();
  double best = std::numeric_limits<double>::max();
  bool hit = false;
  for (int f = 0; f < r.FaceCount(); ++f) {
    const ON_MeshFace& face = r.m_F[f];
    const Point3d v0 = r.Vertex(face.vi[0]), v1 = r.Vertex(face.vi[1]), v2 = r.Vertex(face.vi[2]);
    double t;
    if (RayTriangleHit(p, d, v0, v1, v2, t) && t > 1e-9 && t < best) { best = t; hit = true; }
    if (!face.IsTriangle() && RayTriangleHit(p, d, v0, v2, r.Vertex(face.vi[3]), t) && t > 1e-9 && t < best) { best = t; hit = true; }
  }
  if (!hit) return std::nullopt;
  return p + d * best;
}

// Self-intersection sampling test (CurveSelfIntersects) now lives in
// cmd_common.h, shared with any command that needs to reject a
// self-crossing boundary before treating it as a simple loop.

// Takes the next typed token, or prompts for text.
class TextArgCommand : public Command {
 public:
  TextArgCommand(std::string prompt, std::function<void(CommandContext&, const std::string&)> fn)
      : prompt_(std::move(prompt)), fn_(std::move(fn)) {}
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { fn_(ctx, *t); Finish(); return; }
    WantText(prompt_);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { fn_(ctx, t); Finish(); }
  void OnEnter(CommandContext&) override { Finish(); }

 private:
  std::string prompt_;
  std::function<void(CommandContext&, const std::string&)> fn_;
};

// Takes two typed tokens (key, value), or prompts for each.
class KeyValueCommand : public Command {
 public:
  explicit KeyValueCommand(std::function<void(CommandContext&, const std::string&, const std::string&)> fn) : fn_(std::move(fn)) {}
  void Begin(CommandContext& ctx) override {
    if (auto k = ctx.Engine().TakePendingInput()) {
      key_ = *k;
      if (auto v = ctx.Engine().TakePendingInput()) { fn_(ctx, *key_, *v); Finish(); return; }
      WantText("Value");
      return;
    }
    WantText("Key");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (!key_) { key_ = t; WantText("Value"); return; }
    fn_(ctx, *key_, t);
    Finish();
  }
  void OnEnter(CommandContext&) override { Finish(); }

 private:
  std::optional<std::string> key_;
  std::function<void(CommandContext&, const std::string&, const std::string&)> fn_;
};

// Select: by object name, id, or "All". "Select" with nothing typed prompts.
class SelectCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    bool any = false;
    while (auto t = ctx.Engine().TakePendingInput()) { Apply(ctx, *t); any = true; }
    if (any) { Report(ctx); Finish(); return; }
    WantObjects("Select objects (or type a name, id, or All)", 0);
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) ctx.Doc().Select(id, true);
    Report(ctx);
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { Apply(ctx, t); Report(ctx); Finish(); }
  void OnEnter(CommandContext& ctx) override { Report(ctx); Finish(); }

 private:
  static void Apply(CommandContext& ctx, const std::string& tok) {
    if (Lower(tok) == "all") { ctx.Doc().SelectWhere([&](const SceneObject& o) { return Selectable(ctx, o); }, true); return; }
    char* end = nullptr;
    const unsigned long long id = std::strtoull(tok.c_str(), &end, 10);
    if (end && *end == 0 && id > 0 && ctx.Doc().Find(static_cast<ObjectId>(id))) { ctx.Doc().Select(static_cast<ObjectId>(id), true); return; }
    int n = 0;
    for (SceneObject& o : ctx.Doc().Objects()) if (Lower(o.name) == Lower(tok) && Selectable(ctx, o)) { o.selected = true; ++n; }
    if (!n) ctx.Warn("No object named '" + tok + "'");
  }
};

// Fence / Lasso: pick polygon vertices, Enter closes; objects whose
// bounding-box centre projects inside the polygon are selected.
class SelFenceCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("First fence point"); }
  void OnPoint(CommandContext& ctx, Point3d p) override { pts_.push_back(p); ctx.SetLastPoint(p); WantPoint("Next fence point. Press Enter when done"); }
  void OnEnter(CommandContext& ctx) override {
    ctx.ClearPreview();
    Viewport* vp = ctx.ActiveViewport();
    if (!vp || pts_.size() < 3) { ctx.Warn("A fence needs at least 3 points"); Finish(); return; }
    std::vector<std::pair<double, double>> poly;
    for (const Point3d& p : pts_) { double x, y; if (vp->WorldToPixel(p, x, y)) poly.emplace_back(x, y); }
    if (poly.size() < 3) { Finish(); return; }
    for (SceneObject& o : ctx.Doc().Objects()) {
      if (!Selectable(ctx, o)) continue;
      double x, y;
      if (vp->WorldToPixel(Center(o), x, y) && Inside(poly, x, y)) o.selected = true;
    }
    Report(ctx);
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.empty()) return;
    ctx.ClearPreview();
    std::vector<Point3d> pl = pts_;
    pl.push_back(h);
    ctx.AddPreviewPolyline(pl, pl.size() > 2);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  static bool Inside(const std::vector<std::pair<double, double>>& poly, double x, double y) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
      const double xi = poly[i].first, yi = poly[i].second, xj = poly[j].first, yj = poly[j].second;
      if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
  }
  std::vector<Point3d> pts_;
};

// Circular / Brush: centre and a radius point, measured on screen.
class SelCircularCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Center of selection circle"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!c_) { c_ = p; ctx.SetLastPoint(p); WantPoint("Radius"); return; }
    ctx.ClearPreview();
    Viewport* vp = ctx.ActiveViewport();
    double cx, cy, rx, ry;
    if (vp && vp->WorldToPixel(*c_, cx, cy) && vp->WorldToPixel(p, rx, ry)) {
      const double r2 = (rx - cx) * (rx - cx) + (ry - cy) * (ry - cy);
      for (SceneObject& o : ctx.Doc().Objects()) {
        if (!Selectable(ctx, o)) continue;
        double x, y;
        if (vp->WorldToPixel(Center(o), x, y) && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r2) o.selected = true;
      }
      Report(ctx);
    }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!c_) return;
    ctx.ClearPreview();
    ON_Circle circle(ActivePlane(ctx), *c_, (h - *c_).Length());
    if (circle.IsValid()) ctx.AddPreviewPolyline(CirclePoints(circle), true);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  std::optional<Point3d> c_;
};

// SelBrush / SelBrushPoints: center, then a radius point (same first two
// picks as SelCircular - the first stamp lands as soon as the radius is
// set), then repeated clicks continue the stroke at that same radius until
// Enter, each one stamping on the spot like a brush. SelBrushPoints stamps
// control points (turning PointsOn on as needed and adding to the
// sub-object selection, like SelControlPointRegion's rectangle but per
// circular stamp) instead of whole objects.
class SelBrushCommand : public Command {
 public:
  explicit SelBrushCommand(bool points) : points_(points) {}
  void Begin(CommandContext&) override { WantPoint("Center of brush stroke"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!center_) { center_ = p; ctx.SetLastPoint(p); WantPoint("Radius point (sets the brush size and stamps once)"); return; }
    if (!have_radius_) {
      radius_ = std::max((p - *center_).Length(), 1e-6);
      have_radius_ = true;
      Stamp(ctx, *center_);
    } else {
      Stamp(ctx, p);
    }
    ctx.SetLastPoint(p);
    WantPoint("Next point along the brush stroke, Enter when done");
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    const double r = have_radius_ ? radius_ : (center_ ? (h - *center_).Length() : 0.0);
    const Point3d c = have_radius_ ? h : (center_ ? *center_ : h);
    if (!center_ || r <= 0) return;
    ON_Circle circle(ActivePlane(ctx), c, r);
    if (circle.IsValid()) ctx.AddPreviewPolyline(CirclePoints(circle), true);
  }
  void OnEnter(CommandContext& ctx) override {
    ctx.ClearPreview();
    if (points_) ReportSub(ctx, "SelBrushPoints", added_);
    else ctx.Print("SelBrush: " + std::to_string(ctx.Doc().SelectedCount()) + " object(s) selected");
    Finish();
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  std::optional<Point3d> center_;
  bool have_radius_ = false;
  void Stamp(CommandContext& ctx, Point3d center) {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) return;
    double cx, cy;
    if (!vp->WorldToPixel(center, cx, cy)) return;
    ON_Plane pl = ActivePlane(ctx);
    double u, v;
    pl.ClosestPointTo(center, &u, &v);
    double ex, ey;
    if (!vp->WorldToPixel(pl.PointAt(u + radius_, v), ex, ey)) return;
    const double r2 = (ex - cx) * (ex - cx) + (ey - cy) * (ey - cy);
    if (points_) {
      for (SceneObject& o : ctx.Doc().Objects()) {
        if (!Selectable(ctx, o)) continue;
        const int n = ControlPointCount(o);
        for (int i = 0; i < n; ++i) {
          Point3d cp;
          double x, y;
          if (!ControlPointPosition(o, i, cp) || !vp->WorldToPixel(cp, x, y)) continue;
          if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r2) continue;
          if (!o.show_control_points) { o.show_control_points = true; o.InvalidateDisplay(); }
          SubObjectRef r = SubObjectRef::Vertex(o.id, i);
          if (!Sub(ctx).Contains(r)) { Sub(ctx).Add(r); ++added_; }
        }
      }
    } else {
      for (SceneObject& o : ctx.Doc().Objects()) {
        if (!Selectable(ctx, o) || o.selected) continue;
        double x, y;
        if (vp->WorldToPixel(Center(o), x, y) && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r2) o.selected = true;
      }
    }
  }
  bool points_;
  double radius_ = 0;
  size_t added_ = 0;
};

// SelVolumePipe: two axis points and a radius; selects objects whose
// bounding-box centre lies inside the cylinder.
class SelVolumePipeCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Start of pipe axis"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) { WantPoint("End of pipe axis"); return; }
    if (pts_.size() == 2) { WantNumber("Pipe radius", ctx.Settings().grid_spacing * 5); return; }
    OnNumber(ctx, (p - pts_[1]).Length());
  }
  void OnNumber(CommandContext& ctx, double r) override {
    if (pts_.size() < 2) return;
    const Vector3d axis = pts_[1] - pts_[0];
    const double len = axis.Length();
    if (len < 1e-12 || r <= 0) { ctx.Warn("Degenerate pipe"); Finish(); return; }
    int n = 0;
    for (SceneObject& o : ctx.Doc().Objects()) {
      if (!Selectable(ctx, o)) continue;
      const Vector3d d = Center(o) - pts_[0];
      const double t = (d * axis) / (len * len);
      if (t < 0 || t > 1) continue;
      const double dist = (d - axis * t).Length();
      if (dist <= r) { o.selected = true; ++n; }
    }
    ctx.Print("SelVolumePipe: radius " + FormatNumber(r) + ", " + std::to_string(n) + " object(s) selected");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e = nullptr; double v = std::strtod(t.c_str(), &e); if (e && *e == 0) OnNumber(ctx, v); }
  void OnHover(CommandContext& ctx, Point3d h) override { if (pts_.size() == 1) { ctx.ClearPreview(); ctx.AddPreviewLine(pts_[0], h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  std::vector<Point3d> pts_;
};

// SelShortCrv: curves shorter than a typed length.
class SelShortCrvCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { OnNumber(ctx, std::strtod(t->c_str(), nullptr)); return; }
    WantNumber("Maximum length", ctx.Settings().grid_spacing);
  }
  void OnNumber(CommandContext& ctx, double lim) override {
    int n = 0;
    for (SceneObject& o : ctx.Doc().Objects()) if (o.kind == ObjectKind::Curve && Selectable(ctx, o) && o.curve->Length() < lim) { o.selected = true; ++n; }
    ctx.Print("SelShortCrv: " + std::to_string(n) + " curve(s) shorter than " + FormatNumber(lim) + " selected");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { OnNumber(ctx, std::strtod(t.c_str(), nullptr)); }
};

CommandFactory FilterFlag(std::function<bool&(AppState&)> get, const char* label) {
  return Immediate([get, label](CommandContext& ctx) {
    AppState& st = ctx.App().State();
    bool& b = get(st);
    b = !b;
    ctx.Print(std::string("Selection filter ") + label + (b ? " on" : " off") + " (edges " + (st.filter_edges ? "on" : "off") + ", faces " +
              (st.filter_faces ? "on" : "off") + ", vertices " + (st.filter_vertices ? "on" : "off") + ", filter " + (st.filter_enabled ? "enabled" : "disabled") + ")");
  });
}

}  // namespace

void RegisterSelect2Commands(CommandEngine& e) {
  const char* sub = "Sub-object (edge/face/vertex/control point) selection is coming; whole objects for now.";

  Reg(e, "Select", Make<SelectCommand>());
  Reg(e, "SelDupAll", Immediate([](CommandContext& ctx) {
        const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-9);
        auto& objs = ctx.Doc().Objects();
        int n = 0;
        for (size_t i = 0; i < objs.size(); ++i) for (size_t j = i + 1; j < objs.size(); ++j) {
          if (objs[i].kind != objs[j].kind || PointCount(objs[i]) != PointCount(objs[j])) continue;
          kernel::BoundingBox a = objs[i].BoundingBox(), b = objs[j].BoundingBox();
          if ((a.min - b.min).Length() > tol || (a.max - b.max).Length() > tol) continue;
          if (!Selectable(ctx, objs[i]) || !Selectable(ctx, objs[j])) continue;
          if (!objs[i].selected) { objs[i].selected = true; ++n; }
          if (!objs[j].selected) { objs[j].selected = true; ++n; }
        }
        ctx.Print("SelDupAll: " + std::to_string(n) + " duplicate object(s) selected");
      }));
  Reg(e, "SelFence", Make<SelFenceCommand>());
  Reg(e, "Lasso", Make<SelFenceCommand>());
  Reg(e, "SelCircular", Make<SelCircularCommand>());
  Reg(e, "SelRectangular", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("SelWindow"); }));
  Reg(e, "SelBrush", Make<SelBrushCommand>(false), CommandStatus::Implemented, "Paints a stroke of circular stamps (one per click along the path) instead of just one circle.");
  Reg(e, "SelBrushPoints", Make<SelBrushCommand>(true), CommandStatus::Implemented, "Paints a stroke that adds the control points (not whole objects) under it to the sub-object selection, turning PointsOn as needed.");

  // Annotation, hatch and other tagged groups.
  Reg(e, "SelText", SelGroupNamed({"Text", "TextObject"}));
  Reg(e, "SelDot", SelWhere([](CommandContext&, const SceneObject& o) { return o.user_text.count("Dot") > 0; }));
  Reg(e, "SelLeader", SelGroupNamed({"Leader"}));
  Reg(e, "SelHatch", SelGroupNamed({"Hatch"}));
  Reg(e, "SelDim", SelGroupNamed({"DimLinear", "DimAligned", "DimAngle", "DimRadius", "DimDiameter"}));
  Reg(e, "SelDimLinear", SelGroupNamed({"DimLinear", "DimAligned"}));
  Reg(e, "SelDimAngular", SelGroupNamed({"DimAngle"}));
  Reg(e, "SelDimRadial", SelGroupNamed({"DimRadius", "DimDiameter"}));
  Reg(e, "SelDimOrdinate", NoSuchObjects("ordinate dimensions"));
  Reg(e, "SelDimCentermark", NoSuchObjects("centermarks"));
  Reg(e, "SelLight", NoSuchObjects("lights"));
  Reg(e, "SelPtCloud", SelGroupNamed({"PointCloud"}));
  Reg(e, "SelPicture", NoSuchObjects("pictures"));
  Reg(e, "SelClippingPlane", NoSuchObjects("clipping planes"));
  Reg(e, "SelDetail", NoSuchObjects("details"));
  Reg(e, "SelMappingWidget", NoSuchObjects("mapping widgets"));
  Reg(e, "SelNamedViewWidget", NoSuchObjects("named view widgets"));

  Reg(e, "SelShortCrv", Make<SelShortCrvCommand>());
  Reg(e, "SelSelfIntersectingCrv", Immediate([](CommandContext& ctx) {
        const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-9);
        int n = 0;
        for (SceneObject& o : ctx.Doc().Objects()) if (o.kind == ObjectKind::Curve && Selectable(ctx, o) && CurveSelfIntersects(*o.curve, tol)) { o.selected = true; ++n; }
        ctx.Print("SelSelfIntersectingCrv: " + std::to_string(n) + " curve(s) selected");
      }), CommandStatus::Implemented, "Tests a dense polyline sampling of each curve (at least 64 points, or 12 per control point) for segment/segment intersections within tolerance.");
  Reg(e, "SelValue", Make<TextArgCommand>("User text value", [](CommandContext& ctx, const std::string& v) {
        ctx.Doc().SelectWhere([&](const SceneObject& o) { if (!Selectable(ctx, o)) return false; for (const auto& kv : o.user_text) if (kv.second == v) return true; return false; }, true);
        Report(ctx);
      }));
  Reg(e, "SelKey", Make<TextArgCommand>("User text key", [](CommandContext& ctx, const std::string& k) {
        ctx.Doc().SelectWhere([&](const SceneObject& o) { return Selectable(ctx, o) && o.user_text.count(k) > 0; }, true);
        Report(ctx);
      }));
  Reg(e, "SelKeyValue", Make<KeyValueCommand>([](CommandContext& ctx, const std::string& k, const std::string& v) {
        ctx.Doc().SelectWhere([&](const SceneObject& o) { auto it = o.user_text.find(k); return Selectable(ctx, o) && it != o.user_text.end() && it->second == v; }, true);
        ctx.Print("SelKeyValue: " + std::to_string(ctx.Doc().SelectedCount()) + " object(s) with " + k + "=" + v + " selected");
      }));
  Reg(e, "SelLayerNumber", Make<TextArgCommand>("Layer number", [](CommandContext& ctx, const std::string& t) {
        const int idx = std::atoi(t.c_str());
        if (idx < 0 || idx >= static_cast<int>(ctx.Doc().Layers().size())) { ctx.Warn("No layer " + t); return; }
        ctx.Doc().SelectWhere([&](const SceneObject& o) { return Selectable(ctx, o) && o.layer_index == idx; }, true);
        ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) on layer " + std::to_string(idx) + " (" + ctx.Doc().LayerFullPath(idx) + ")");
      }));

  // Volume selection.
  Reg(e, "SelVolumeSphere", Make<PointThenDistanceCommand>("Center of sphere", "Radius", [](CommandContext& ctx, Point3d c, double r, Point3d) {
        int n = 0;
        for (SceneObject& o : ctx.Doc().Objects()) if (Selectable(ctx, o) && (Center(o) - c).Length() <= r) { o.selected = true; ++n; }
        ctx.Print("SelVolumeSphere: radius " + FormatNumber(r) + ", " + std::to_string(n) + " object(s) selected");
      }));
  Reg(e, "SelVolumePipe", Make<SelVolumePipeCommand>());
  Reg(e, "SelVolumeObject", OnSelection("Select a closed object to use as the selection volume", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        const SceneObject* vol = ctx.Doc().Find(ids.front());
        std::optional<kernel::Mesh> m = vol ? MeshOf(*vol) : std::nullopt;
        if (!m || !m->IsClosedManifold()) { ctx.Warn("The volume object must be a closed solid or mesh"); return; }
        ctx.Doc().SelectNone();
        int n = 0;
        for (SceneObject& o : ctx.Doc().Objects()) if (o.id != vol->id && Selectable(ctx, o) && m->ContainsPoint(Center(o))) { o.selected = true; ++n; }
        ctx.Print("SelVolumeObject: " + std::to_string(n) + " object(s) inside object " + std::to_string(vol->id));
      }));
  Reg(e, "SelConnected", Immediate([](CommandContext& ctx) {
        const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-9);
        if (ctx.Doc().SelectedCount() == 0) { ctx.Warn("Select the seed objects first"); return; }
        bool grew = true;
        int added = 0;
        while (grew) {
          grew = false;
          for (SceneObject& o : ctx.Doc().Objects()) {
            if (o.selected || !Selectable(ctx, o)) continue;
            kernel::BoundingBox a = o.BoundingBox();
            for (const SceneObject& s : ctx.Doc().Objects()) if (s.selected && BoxesTouch(a, s.BoundingBox(), tol)) { o.selected = true; grew = true; ++added; break; }
          }
        }
        ctx.Print("SelConnected: " + std::to_string(added) + " connected object(s) added, " + std::to_string(ctx.Doc().SelectedCount()) + " selected");
      }));
  Reg(e, "SelExtrusion", SelWhere([](CommandContext&, const SceneObject& o) { return o.kind == ObjectKind::Brep; }), CommandStatus::Partial, "Dino 8 stores extrusions as polysurfaces; selects every polysurface.");
  Reg(e, "SelTrimmedSrf", SelWhere([](CommandContext&, const SceneObject& o) { return o.kind == ObjectKind::Brep && o.brep->raw().m_F.Count() == 1 && !o.brep->raw().FaceIsSurface(0); }));
  Reg(e, "SelUntrimmedSrf", SelWhere([](CommandContext&, const SceneObject& o) { return o.kind == ObjectKind::Surface || (o.kind == ObjectKind::Brep && o.brep->raw().m_F.Count() == 1 && o.brep->raw().FaceIsSurface(0)); }));
  Reg(e, "SelClosedSubD", SelWhere([](CommandContext&, const SceneObject& o) { return o.kind == ObjectKind::SubD && o.subd->raw().IsSolid(); }));
  Reg(e, "SelOpenSubD", SelWhere([](CommandContext&, const SceneObject& o) { return o.kind == ObjectKind::SubD && !o.subd->raw().IsSolid(); }));

  auto group_members = [](const char* label) {
    return Immediate([label](CommandContext& ctx) {
      std::set<int> groups;
      for (const SceneObject& o : ctx.Doc().Objects()) if (o.selected && o.group_id >= 0) groups.insert(o.group_id);
      if (groups.empty()) { ctx.Print(std::string(label) + ": the selection has no groups"); return; }
      ctx.Doc().SelectWhere([&](const SceneObject& o) { return Selectable(ctx, o) && groups.count(o.group_id) > 0; }, true);
      Report(ctx);
    });
  };
  Reg(e, "SelChildren", group_members("SelChildren"), CommandStatus::Partial, "Selects the other members of the selected objects' groups.");
  Reg(e, "SelParents", group_members("SelParents"), CommandStatus::Partial, "Selects the other members of the selected objects' groups.");
  Reg(e, "SelCaptives", group_members("SelCaptives"), CommandStatus::Partial, "Selects the other members of the selected objects' groups.");

  Reg(e, "SelControlPoint", Immediate([](CommandContext& ctx) {
        size_t added = 0;
        for (SceneObject& o : ctx.Doc().Objects()) {
          if (!o.selected) continue;
          const int n = ControlPointCount(o);
          if (n == 0) continue;
          if (!o.show_control_points) { o.show_control_points = true; o.InvalidateDisplay(); }
          for (int i = 0; i < n; ++i) { SubObjectRef r = SubObjectRef::Vertex(o.id, i); if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
        }
        if (added == 0 && Sub(ctx).Empty()) ctx.Warn("Select curves, surfaces, meshes or SubDs first");
        else ReportSub(ctx, "SelControlPoint", added);
      }), CommandStatus::Implemented, "Turns on and selects every control point of the selected objects.");
  Reg(e, "SelControls", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("SelControlPoint"); }));
  Reg(e, "SelControlPointRegion", Make<PointsCommand>(
        std::vector<std::string>{"First corner of the region", "Opposite corner"},
        [](CommandContext& ctx, const std::vector<Point3d>& p) {
          const Point3d lo(std::min(p[0].x, p[1].x), std::min(p[0].y, p[1].y), std::min(p[0].z, p[1].z) - 1e6);
          const Point3d hi(std::max(p[0].x, p[1].x), std::max(p[0].y, p[1].y), std::max(p[0].z, p[1].z) + 1e6);
          size_t added = 0;
          for (const SceneObject& o : ctx.Doc().Objects()) {
            if (!o.show_control_points) continue;
            const int n = ControlPointCount(o);
            for (int i = 0; i < n; ++i) {
              Point3d cp;
              if (!ControlPointPosition(o, i, cp)) continue;
              if (cp.x < lo.x || cp.x > hi.x || cp.y < lo.y || cp.y > hi.y || cp.z < lo.z || cp.z > hi.z) continue;
              SubObjectRef r = SubObjectRef::Vertex(o.id, i);
              if (!Sub(ctx).Contains(r)) ++added;
              Sub(ctx).Add(r);
            }
          }
          ReportSub(ctx, "SelControlPointRegion", added);
        }), CommandStatus::Implemented, "Selects control points of objects with PointsOn inside a CPlane-aligned box between two picked points.");
  Reg(e, "SelU", SelControlPointGrid("SelU", true, false));
  Reg(e, "SelV", SelControlPointGrid("SelV", false, true));
  Reg(e, "SelUV", SelControlPointGrid("SelUV", true, true));
  Reg(e, "SelEdgeLoop", Immediate([](CommandContext& ctx) {
        std::vector<SubObjectRef> seeds = SelectedOfKind(ctx, SubObjectKind::Edge);
        if (seeds.empty()) { ctx.Warn("Ctrl+Shift-click a mesh or SubD edge first"); return; }
        size_t added = 0;
        for (const SubObjectRef& seed : seeds) {
          const SceneObject* o = ctx.Doc().Find(seed.id);
          if (!o) continue;
          for (const SubObjectRef& r : EdgeLoop(*o, seed)) { if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
        }
        ReportSub(ctx, "SelEdgeLoop", added);
      }), CommandStatus::Implemented, "Walks the mesh / SubD control-net quad topology from a selected edge.");
  Reg(e, "SelEdgeRing", Immediate([](CommandContext& ctx) {
        std::vector<SubObjectRef> seeds = SelectedOfKind(ctx, SubObjectKind::Edge);
        if (seeds.empty()) { ctx.Warn("Ctrl+Shift-click a mesh or SubD edge first"); return; }
        size_t added = 0;
        for (const SubObjectRef& seed : seeds) {
          const SceneObject* o = ctx.Doc().Find(seed.id);
          if (!o) continue;
          for (const SubObjectRef& r : EdgeRing(*o, seed)) { if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
        }
        ReportSub(ctx, "SelEdgeRing", added);
      }), CommandStatus::Implemented, "Walks the opposite-edge ring of a selected mesh / SubD edge.");
  Reg(e, "SelFaceLoop", Immediate([](CommandContext& ctx) {
        std::vector<SubObjectRef> seeds = SelectedOfKind(ctx, SubObjectKind::Edge);
        if (seeds.empty()) { ctx.Warn("Ctrl+Shift-click a mesh or SubD edge first"); return; }
        size_t added = 0;
        for (const SubObjectRef& seed : seeds) {
          const SceneObject* o = ctx.Doc().Find(seed.id);
          if (!o) continue;
          for (const SubObjectRef& r : FaceLoop(*o, seed)) { if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
        }
        ReportSub(ctx, "SelFaceLoop", added);
      }), CommandStatus::Implemented, "Selects the row of faces straddling a selected mesh / SubD edge.");
  Reg(e, "SelFacesToBoundary", Immediate([](CommandContext& ctx) {
        std::vector<SubObjectRef> boundary = SelectedOfKind(ctx, SubObjectKind::Edge);
        std::vector<SubObjectRef> faceSeeds = SelectedOfKind(ctx, SubObjectKind::Face);
        if (faceSeeds.empty() || boundary.empty()) { ctx.Warn("Ctrl+Shift-click at least one seed face and the boundary edges first"); return; }
        std::map<ObjectId, std::vector<int>> seedsByObject;
        for (const SubObjectRef& r : faceSeeds) seedsByObject[r.id].push_back(r.index);
        size_t added = 0;
        for (auto& [id, seeds] : seedsByObject) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          for (const SubObjectRef& r : FacesToBoundary(*o, seeds, boundary)) { if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
        }
        ReportSub(ctx, "SelFacesToBoundary", added);
      }), CommandStatus::Implemented, "Grows the selected seed face(s) until it reaches the selected boundary edges.");
  Reg(e, "SelMeshEdges", Immediate([](CommandContext& ctx) {
        size_t added = 0;
        int objs = 0;
        for (const SceneObject& o : ctx.Doc().Objects()) {
          if (!o.selected || (o.kind != ObjectKind::Mesh && o.kind != ObjectKind::SubD)) continue;
          ++objs;
          for (const SubObjectRef& r : AllEdges(o)) { if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
        }
        if (objs == 0) ctx.Warn("Select meshes or SubDs first");
        else ReportSub(ctx, "SelMeshEdges", added);
      }), CommandStatus::Implemented, "Selects every edge of the selected meshes / SubDs.");
  Reg(e, "SelSubDEdges", Immediate([](CommandContext& ctx) {
        size_t added = 0;
        int objs = 0;
        for (const SceneObject& o : ctx.Doc().Objects()) {
          if (!o.selected || o.kind != ObjectKind::SubD) continue;
          ++objs;
          for (const SubObjectRef& r : SubDCreaseEdges(o)) { if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
        }
        if (objs == 0) ctx.Warn("Select SubDs first");
        else ReportSub(ctx, "SelSubDEdges", added);
      }), CommandStatus::Implemented, "Selects the crease edges of the selected SubDs as sub-objects.");
  Reg(e, "SelMeshPart", Make<PickSubObjectCommand>(
        "Click on a mesh or SubD to select the connected part",
        [](CommandContext& ctx, Point3d p) -> std::optional<SubObjectRef> {
          Viewport* vp = ctx.ActiveViewport();
          if (!vp) return std::nullopt;
          double sx, sy;
          if (!vp->WorldToPixel(p, sx, sy)) return std::nullopt;
          SubObjectPickFilter f;
          f.faces = true;
          std::optional<SubObjectPick> pick = vp->PickSubObject(ctx.Doc(), sx, sy, f, 1e9);
          if (!pick || pick->ref.kind != SubObjectKind::Face) return std::nullopt;
          return pick->ref;
        },
        [](CommandContext& ctx, const SubObjectRef& seed) {
          const SceneObject* o = ctx.Doc().Find(seed.id);
          if (!o) return;
          size_t added = 0;
          for (const SubObjectRef& r : ConnectedFaces(*o, seed.index)) { if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
          ReportSub(ctx, "SelMeshPart", added);
        }), CommandStatus::Implemented, "Selects the faces connected to the clicked face (one mesh 'part').");
  Reg(e, "SelNakedMeshEdgePt", Immediate([](CommandContext& ctx) {
        size_t added = 0;
        int objs = 0;
        for (const SceneObject& o : ctx.Doc().Objects()) {
          if (!o.selected || (o.kind != ObjectKind::Mesh && o.kind != ObjectKind::SubD)) continue;
          ++objs;
          for (const SubObjectRef& r : NakedEdgeVertices(o)) { if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
        }
        if (objs == 0) ctx.Warn("Select meshes or SubDs first");
        else ReportSub(ctx, "SelNakedMeshEdgePt", added);
      }), CommandStatus::Implemented, "Selects the vertices of naked (boundary) mesh / SubD edges as points.");
  Reg(e, "SelNonManifold", Immediate([](CommandContext& ctx) {
        size_t added = 0;
        int objs = 0;
        for (const SceneObject& o : ctx.Doc().Objects()) {
          if ((o.kind != ObjectKind::Mesh && o.kind != ObjectKind::SubD) || !Selectable(ctx, o)) continue;
          std::vector<SubObjectRef> nm = NonManifoldEdges(o);
          if (nm.empty()) continue;
          ++objs;
          for (const SubObjectRef& r : nm) { if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
        }
        if (objs == 0) ctx.Print("SelNonManifold: no non-manifold edges found");
        else ReportSub(ctx, "SelNonManifold", added);
      }), CommandStatus::Implemented, "Selects the individual edges shared by more than two faces, as sub-objects (not whole meshes).");

  // Blocks.
  Reg(e, "SelMirroredBlocks", SelWhere([](CommandContext&, const SceneObject& o) { return o.user_text.count("Block") > 0 && o.user_text.count("Mirrored") > 0; }), CommandStatus::Partial, "Block instances are not tracked as mirrored yet; selects instances tagged Mirrored.");
  Reg(e, "SelObjectsWithHistory", Immediate([](CommandContext& ctx) { ctx.Print("0 objects selected (Dino 8 keeps no construction history; every edit is undoable instead)"); }));

  // Attributes.
  Reg(e, "SelRenderColor", Immediate([](CommandContext& ctx) {
        const SceneObject* ref = nullptr;
        for (const SceneObject& o : ctx.Doc().Objects()) if (o.selected) { ref = &o; break; }
        if (!ref) { ctx.Doc().SelectWhere([&](const SceneObject& o) { return Selectable(ctx, o) && !o.color_by_layer; }); }
        else { const Color c = ref->color; ctx.Doc().SelectWhere([&](const SceneObject& o) { return Selectable(ctx, o) && !o.color_by_layer && std::fabs(o.color.r - c.r) < 0.01 && std::fabs(o.color.g - c.g) < 0.01 && std::fabs(o.color.b - c.b) < 0.01; }); }
        Report(ctx);
      }));
  Reg(e, "SelMaterialName", Make<TextArgCommand>("Material name", [](CommandContext& ctx, const std::string& name) {
        ctx.Doc().SelectWhere([&](const SceneObject& o) { return Selectable(ctx, o) && Lower(o.material_name) == Lower(name); }, true);
        Report(ctx);
      }));
  Reg(e, "SelFontUse", SelGroupNamed({"Text", "TextObject", "Leader", "DimLinear", "DimAligned", "DimAngle", "DimRadius", "DimDiameter"}), CommandStatus::Implemented,
      "Selects every annotation object: the document has one font setting (Document Properties > Text), so every annotation uses it.");
  Reg(e, "SelAnnotationStyle", SelGroupNamed({"Text", "TextObject", "Leader", "DimLinear", "DimAligned", "DimAngle", "DimRadius", "DimDiameter"}), CommandStatus::Implemented,
      "Selects every annotation object: there is one dimension/text style for the whole document, so every annotation uses it.");
  Reg(e, "SelDimOverride", NoSuchObjects("dimensions with style overrides"));
  Reg(e, "SelDimTextOverride", NoSuchObjects("dimensions with text overrides"));
  Reg(e, "SelSubDFriendlyCrv", SelWhere([](CommandContext&, const SceneObject& o) { return o.kind == ObjectKind::Curve && o.curve->Degree() == 3; }));

  // Selection filter flags.
  Reg(e, "SelectionFilterEdges", FilterFlag([](AppState& s) -> bool& { return s.filter_edges; }, "edges"));
  Reg(e, "SelectionFilterFaces", FilterFlag([](AppState& s) -> bool& { return s.filter_faces; }, "faces"));
  Reg(e, "SelectionFilterVertices", FilterFlag([](AppState& s) -> bool& { return s.filter_vertices; }, "vertices"));
  Reg(e, "SelectionFilterEnable", FilterFlag([](AppState& s) -> bool& { return s.filter_enabled; }, "enable"));
  Reg(e, "SelectionFilterToggle", FilterFlag([](AppState& s) -> bool& { return s.filter_enabled; }, "toggle"));
  Reg(e, "SelectionFilterNone", Immediate([](CommandContext& ctx) { AppState& s = ctx.App().State(); s.filter_edges = s.filter_faces = s.filter_vertices = false; ctx.Print("Selection filter: whole objects only"); }));

  // Groups.
  Reg(e, "UngroupAll", Immediate([](CommandContext& ctx) {
        std::vector<ObjectId> ids;
        for (const SceneObject& o : ctx.Doc().Objects()) if (o.group_id >= 0) ids.push_back(o.id);
        if (ids.empty()) { ctx.Print("No groups"); return; }
        ctx.Doc().BeginChange("UngroupAll");
        ctx.Doc().Ungroup(ids);
        ctx.Print("UngroupAll: " + std::to_string(ids.size()) + " object(s) ungrouped");
      }));
  Reg(e, "SetGroupName", Make<TextArgCommand>("Group name", [](CommandContext& ctx, const std::string& name) {
        std::set<int> groups;
        for (const SceneObject& o : ctx.Doc().Objects()) if (o.selected && o.group_id >= 0) groups.insert(o.group_id);
        if (groups.empty()) { ctx.Warn("Select a grouped object first"); return; }
        ctx.Doc().BeginChange("SetGroupName");
        for (int g : groups) if (Group* grp = ctx.Doc().FindGroup(g)) grp->name = name;
        ctx.Doc().Touch();
        ctx.Print("SetGroupName: " + std::to_string(groups.size()) + " group(s) named '" + name + "'");
      }));
  Reg(e, "UndoSelected", Immediate([](CommandContext& ctx) { if (!ctx.Doc().Undo()) ctx.Print("Nothing to undo"); }), CommandStatus::Partial, "Undoes the last change to the whole document.");
  Reg(e, "HidePt", Immediate([](CommandContext& ctx) {
        // With a control-point selection, hide only those points (and drop
        // them from the selection); otherwise fall back to hiding the
        // whole grip display of the selected objects.
        if (!Sub(ctx).Empty()) {
          std::map<ObjectId, std::vector<SubObjectRef>> byObj;
          for (const SubObjectRef& r : Sub(ctx).Items()) if (r.kind == SubObjectKind::Vertex) byObj[r.id].push_back(r);
          int n = 0;
          for (auto& [id, refs] : byObj) {
            SceneObject* o = ctx.Doc().Find(id);
            if (!o) continue;
            for (const SubObjectRef& r : refs) { o->hidden_control_points.push_back(r.index); Sub(ctx).Remove(r); ++n; }
            o->InvalidateDisplay();
          }
          ctx.Print("HidePt: " + std::to_string(n) + " control point(s) hidden");
          return;
        }
        int n = 0;
        for (SceneObject& o : ctx.Doc().Objects()) if (o.selected && o.show_control_points) { o.show_control_points = false; ++n; }
        ctx.Print("HidePt: control points hidden on " + std::to_string(n) + " object(s)");
      }), CommandStatus::Implemented, "Hides the selected control points (or, with none picked, every grip of the selected objects).");
  Reg(e, "ShowPt", Immediate([](CommandContext& ctx) {
        int n = 0;
        for (SceneObject& o : ctx.Doc().Objects()) {
          if (!o.selected) continue;
          o.show_control_points = true;
          if (!o.hidden_control_points.empty()) { o.hidden_control_points.clear(); o.InvalidateDisplay(); }
          ++n;
        }
        ctx.Print("ShowPt: control points shown on " + std::to_string(n) + " object(s)");
      }));
  Reg(e, "PtOffSelected", Immediate([](CommandContext& ctx) {
        int n = 0;
        for (SceneObject& o : ctx.Doc().Objects()) if (o.selected && o.show_control_points) { o.show_control_points = false; o.InvalidateDisplay(); ++n; }
        for (ObjectId id : Sub(ctx).ObjectIds()) Sub(ctx).RemoveObject(id);
        ctx.Print("PtOffSelected: control points turned off on " + std::to_string(n) + " selected object(s)");
      }));
  Reg(e, "CullControlPolygon", Immediate([](CommandContext& ctx) {
        bool& c = ctx.App().State().cull_control_polygon;
        c = !c;
        ctx.Print(std::string("CullControlPolygon: ") + (c ? "on (only unoccluded control points are pickable)" : "off"));
      }), CommandStatus::Implemented, "Front-facing filter for control-point picking, driven by ray-occlusion against the object's own display mesh.");
  Reg(e, "InvertPt", Immediate([](CommandContext& ctx) {
        // Invert within the currently PointsOn objects' control points.
        std::set<ObjectId> withPoints;
        for (const SceneObject& o : ctx.Doc().Objects()) if (o.show_control_points) withPoints.insert(o.id);
        if (withPoints.empty()) { ctx.Print("InvertPt: no objects have PointsOn"); return; }
        SubObjectSelection fresh;
        for (ObjectId id : withPoints) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          const int n = ControlPointCount(*o);
          for (int i = 0; i < n; ++i) {
            SubObjectRef r = SubObjectRef::Vertex(id, i);
            if (!Sub(ctx).Contains(r)) fresh.Add(r);
          }
        }
        Sub(ctx) = fresh;
        ctx.Print("InvertPt: " + std::to_string(Sub(ctx).Size()) + " control point(s) selected");
      }), CommandStatus::Implemented, "Inverts the control-point selection within the objects that have PointsOn.");
  // ---- point-edit navigation (NextU/PrevU/... move or extend the CV
  // selection along a surface's or curve's parameter grid) ------------------
  auto shift_grid = [](CommandContext& ctx, int di, int dj, bool add, const char* name) {
    std::vector<SubObjectRef> current = SelectedOfKind(ctx, SubObjectKind::Vertex);
    if (current.empty()) { ctx.Warn(std::string(name) + ": select a control point first (PointsOn, then click one)"); return; }
    std::vector<SubObjectRef> shifted;
    for (const SubObjectRef& r : current) {
      const SceneObject* o = ctx.Doc().Find(r.id);
      if (!o) continue;
      if (o->kind == ObjectKind::Surface) {
        int nu, nv;
        SurfaceGrid(*o, nu, nv);
        const int i = std::clamp(r.index / nv + di, 0, nu - 1);
        const int j = std::clamp(r.index % nv + dj, 0, nv - 1);
        shifted.push_back(SubObjectRef::Vertex(r.id, i * nv + j));
      } else if (o->kind == ObjectKind::Curve) {
        const int n = ControlPointCount(*o);
        shifted.push_back(SubObjectRef::Vertex(r.id, std::clamp(r.index + di + dj, 0, n - 1)));
      } else {
        shifted.push_back(r);  // meshes / SubDs: no U/V grid, keep as-is
      }
    }
    if (!add) Sub(ctx).Clear();
    size_t added = 0;
    for (const SubObjectRef& r : shifted) { if (!Sub(ctx).Contains(r)) ++added; Sub(ctx).Add(r); }
    (void)added;
    ctx.Print(std::string(name) + ": " + std::to_string(Sub(ctx).Size()) + " control point(s) selected");
  };
  Reg(e, "NextU", Immediate([shift_grid](CommandContext& ctx) { shift_grid(ctx, 1, 0, false, "NextU"); }), CommandStatus::Implemented, "Moves the control-point selection one step in the surface U direction.");
  Reg(e, "PrevU", Immediate([shift_grid](CommandContext& ctx) { shift_grid(ctx, -1, 0, false, "PrevU"); }), CommandStatus::Implemented, "Moves the control-point selection one step back in U.");
  Reg(e, "NextV", Immediate([shift_grid](CommandContext& ctx) { shift_grid(ctx, 0, 1, false, "NextV"); }), CommandStatus::Implemented, "Moves the control-point selection one step in the surface V direction.");
  Reg(e, "PrevV", Immediate([shift_grid](CommandContext& ctx) { shift_grid(ctx, 0, -1, false, "PrevV"); }), CommandStatus::Implemented, "Moves the control-point selection one step back in V.");
  Reg(e, "AddNextU", Immediate([shift_grid](CommandContext& ctx) { shift_grid(ctx, 1, 0, true, "AddNextU"); }), CommandStatus::Implemented, "Extends the control-point selection one step in U.");
  Reg(e, "AddPrevU", Immediate([shift_grid](CommandContext& ctx) { shift_grid(ctx, -1, 0, true, "AddPrevU"); }), CommandStatus::Implemented, "Extends the control-point selection one step back in U.");
  Reg(e, "AddNextV", Immediate([shift_grid](CommandContext& ctx) { shift_grid(ctx, 0, 1, true, "AddNextV"); }), CommandStatus::Implemented, "Extends the control-point selection one step in V.");
  Reg(e, "AddPrevV", Immediate([shift_grid](CommandContext& ctx) { shift_grid(ctx, 0, -1, true, "AddPrevV"); }), CommandStatus::Implemented, "Extends the control-point selection one step back in V.");

  // ---- typed CV move along the surface's local U, V and normal directions --
  Reg(e, "MoveUVN", Make<TextArgCommand>("Move by U,V,N (model units, comma separated)", [](CommandContext& ctx, const std::string& t) {
        double du = 0, dv = 0, dn = 0;
        std::sscanf(t.c_str(), "%lf,%lf,%lf", &du, &dv, &dn);
        std::vector<SubObjectRef> cps = SelectedOfKind(ctx, SubObjectKind::Vertex);
        if (cps.empty()) { ctx.Warn("Select control points first (PointsOn, then click them)"); return; }
        // The exact set of existing objects whose control points move is
        // known up front from cps (only surfaces below are actually
        // touched, a subset); nothing else about the document changes.
        std::vector<ObjectId> touched_ids;
        for (const SubObjectRef& r : cps) {
          if (std::find(touched_ids.begin(), touched_ids.end(), r.id) == touched_ids.end()) touched_ids.push_back(r.id);
        }
        ctx.Doc().BeginChangeForObjects("MoveUVN", touched_ids);
        int moved = 0;
        std::map<ObjectId, std::vector<SubObjectRef>> byObj;
        for (const SubObjectRef& r : cps) byObj[r.id].push_back(r);
        for (auto& [id, refs] : byObj) {
          SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Surface) continue;
          int nu, nv;
          SurfaceGrid(*o, nu, nv);
          const kernel::Interval du_dom = o->surface->Domain(0), dv_dom = o->surface->Domain(1);
          for (const SubObjectRef& r : refs) {
            const int i = r.index / nv, j = r.index % nv;
            const double u = o->surface->raw().GrevilleAbcissa(0, i), v = o->surface->raw().GrevilleAbcissa(1, j);
            Vector3d nrm = o->surface->NormalAt(u, v);
            if (!nrm.Unitize()) nrm = Vector3d(0, 0, 1);
            Point3d p;
            if (!ControlPointPosition(*o, r.index, p)) continue;
            const double eps = 1e-4 * std::max(du_dom.max - du_dom.min, 1e-9), eps2 = 1e-4 * std::max(dv_dom.max - dv_dom.min, 1e-9);
            const double u2 = std::clamp(u + eps, du_dom.min, du_dom.max), v2 = std::clamp(v + eps2, dv_dom.min, dv_dom.max);
            Vector3d tu = o->surface->PointAt(u2, v) - o->surface->PointAt(u, v);
            Vector3d tv = o->surface->PointAt(u, v2) - o->surface->PointAt(u, v);
            if (!tu.Unitize()) tu = Vector3d(1, 0, 0);
            if (!tv.Unitize()) tv = Vector3d(0, 1, 0);
            SetControlPointPosition(*o, r.index, p + tu * du + tv * dv + nrm * dn);
            ++moved;
          }
          o->InvalidateDisplay();
        }
        ctx.Print("MoveUVN: " + std::to_string(moved) + " control point(s) moved by U=" + FormatNumber(du) + " V=" + FormatNumber(dv) + " N=" + FormatNumber(dn));
      }), CommandStatus::Implemented, "Moves the selected surface control points along the surface's own local U, V and normal directions at each point.");

  // ---- misc point-edit --------------------------------------------------
  Reg(e, "HBar", Immediate([](CommandContext& ctx) { ctx.Print("HBar: turn on control points (PointsOn), select a control point, then use the Gumball to drag it - the two-sided Bezier handlebar UI is planned."); }), CommandStatus::Partial, "Handlebar-style dragging is not drawn; PointsOn + Gumball reaches the same result one CV at a time.");
  Reg(e, "DrapePt", OnSelection("Select points to drape onto the objects below them", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<ObjectId> pts;
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Point) pts.push_back(id); }
        if (pts.empty()) { ctx.Warn("DrapePt: select point objects"); return; }
        std::vector<kernel::Mesh> targets;
        for (const SceneObject& o : ctx.Doc().Objects()) {
          if (std::find(pts.begin(), pts.end(), o.id) != pts.end() || !ctx.Doc().IsObjectVisible(o)) continue;
          if (std::optional<kernel::Mesh> m = MeshOf(o, 0.01)) targets.push_back(std::move(*m));
        }
        if (targets.empty()) { ctx.Warn("DrapePt: no surfaces, polysurfaces, meshes or SubDs to drape onto"); return; }
        // pts is the fixed set of existing point objects being moved;
        // the drape targets are only read, never modified - fast path.
        ctx.Doc().BeginChangeForObjects("DrapePt", pts);
        const Vector3d down(0, 0, -1);
        int n = 0;
        for (ObjectId id : pts) {
          SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          std::optional<Point3d> best;
          for (const kernel::Mesh& m : targets) {
            if (std::optional<Point3d> q = RayHitMesh(m, o->point, down)) {
              if (!best || o->point.DistanceTo(*q) < o->point.DistanceTo(*best)) best = q;
            }
          }
          if (best) { o->point = *best; o->InvalidateDisplay(); ++n; }
        }
        ctx.Doc().Touch();
        ctx.Print("DrapePt: " + std::to_string(n) + " of " + std::to_string(pts.size()) + " point(s) draped onto the object(s) below them");
      }), CommandStatus::Implemented, "Drops the selected points straight down (world -Z) onto the nearest surface/polysurface/mesh/SubD below them.");
}

}  // namespace dino8::app
