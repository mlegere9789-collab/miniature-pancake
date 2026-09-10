// Commands for the parametric 2D sketch constraint solver (see
// sketch/Constraints.h; AUDIT.md row 19, "declined feature: parametric
// constraints", not addressed anywhere else in Dino 8).
#include "commands/cmd_common.h"
#include "sketch/Constraints.h"

namespace dino8::app {

using sketch::Constraint;
using sketch::ConstraintType;
using sketch::PointRef;

namespace {

bool IsCircleOrArc(const SceneObject& o, ON_Arc* out = nullptr) {
  return o.kind == ObjectKind::Curve && o.curve && o.curve->raw().IsArc(nullptr, out, 1e-6);
}

// Points a constraint could reasonably pin on `o`: a circle/arc's centre,
// or either endpoint of a line/polyline.
std::vector<PointRef> CandidatePoints(const SceneObject& o) {
  if (IsCircleOrArc(o)) return {{o.id, -1}};
  if (o.curve && o.curve->ControlPointCount() >= 2) return {{o.id, 0}, {o.id, o.curve->ControlPointCount() - 1}};
  return {};
}

bool RefWorld(const Document& doc, const PointRef& r, Point3d& out) {
  const SceneObject* o = doc.Find(r.object);
  if (!o || !o->curve) return false;
  if (r.index < 0) { ON_Arc arc; if (!IsCircleOrArc(*o, &arc)) return false; out = arc.Center(); return true; }
  if (r.index >= o->curve->ControlPointCount()) return false;
  out = o->curve->ControlPointAt(r.index);
  return true;
}

// The pair of candidate points (one from each object) closest to each
// other in world space - the natural pick for Coincident/Distance between
// two curves picked without also having to click a specific endpoint.
bool ClosestPair(const Document& doc, ObjectId a, ObjectId b, PointRef& ra, PointRef& rb) {
  const SceneObject *oa = doc.Find(a), *ob = doc.Find(b);
  if (!oa || !ob) return false;
  double best = -1;
  for (const PointRef& pa : CandidatePoints(*oa)) {
    for (const PointRef& pb : CandidatePoints(*ob)) {
      Point3d wa, wb;
      if (!RefWorld(doc, pa, wa) || !RefWorld(doc, pb, wb)) continue;
      double d = (wa - wb).Length();
      if (best < 0 || d < best) { best = d; ra = pa; rb = pb; }
    }
  }
  return best >= 0;
}

bool LineEndpoints(const SceneObject& o, PointRef& a, PointRef& b) {
  if (IsCircleOrArc(o) || !o.curve || o.curve->ControlPointCount() < 2) return false;
  a = {o.id, 0};
  b = {o.id, o.curve->ControlPointCount() - 1};
  return true;
}

ConstructionPlane ActiveCPlane(CommandContext& ctx) {
  if (Viewport* vp = ctx.ActiveViewport()) return vp->CPlane();
  return ConstructionPlane{};
}

std::string Prompt(ConstraintType type, int have, int need) {
  return "Select curve " + std::to_string(have + 1) + " of " + std::to_string(need) + " for " + sketch::ConstraintTypeName(type);
}

// ---------------------------------------------------------------------------
// Constrain: options Type=..., then the required number of curve picks
// (1-3 depending on type), then a numeric value for Distance/Angle/Radius.
// ---------------------------------------------------------------------------
class ConstrainCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    UpdateOptions();
    WantObjects(Prompt(type_, 0, sketch::RequiredObjectCount(type_)), 1);
  }
  void OnOption(CommandContext& ctx, const std::string& name, const std::string& value) override {
    if (name != "Type") return;
    ConstraintType t;
    if (sketch::ParseConstraintType(value, t)) type_ = t;
    ids_.clear();
    UpdateOptions();
    accept_preselection = false;
    WantObjects(Prompt(type_, 0, sketch::RequiredObjectCount(type_)), 1);
    (void)ctx;
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& objs) override {
    for (ObjectId id : objs) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (o && o->kind == ObjectKind::Curve && std::find(ids_.begin(), ids_.end(), id) == ids_.end()) ids_.push_back(id);
    }
    const int need = sketch::RequiredObjectCount(type_);
    if (static_cast<int>(ids_.size()) < need) {
      accept_preselection = false;
      WantObjects(Prompt(type_, static_cast<int>(ids_.size()), need), 1);
      return;
    }
    if (sketch::RequiresValue(type_)) {
      WantNumber(ValuePrompt(ctx), DefaultValue(ctx));
      return;
    }
    Build(ctx);
  }
  void OnNumber(CommandContext& ctx, double v) override { value_ = v; Build(ctx); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnEnter(CommandContext& ctx) override { if (default_number) OnNumber(ctx, *default_number); }

 private:
  void UpdateOptions() {
    options = {{"Type", sketch::ConstraintTypeName(type_), sketch::ConstraintTypeNames(), false, false}};
  }
  std::string ValuePrompt(CommandContext&) {
    switch (type_) {
      case ConstraintType::Distance: return "Distance";
      case ConstraintType::Angle: return "Angle in degrees";
      case ConstraintType::Radius: return "Radius";
      default: return "Value";
    }
  }
  std::optional<double> DefaultValue(CommandContext& ctx) {
    if (type_ == ConstraintType::Distance && ids_.size() == 2) {
      PointRef ra, rb;
      if (ClosestPair(ctx.Doc(), ids_[0], ids_[1], ra, rb)) {
        Point3d wa, wb;
        if (RefWorld(ctx.Doc(), ra, wa) && RefWorld(ctx.Doc(), rb, wb)) return (wa - wb).Length();
      }
    }
    if (type_ == ConstraintType::Radius && !ids_.empty()) {
      ON_Arc arc;
      if (const SceneObject* o = ctx.Doc().Find(ids_[0])) if (IsCircleOrArc(*o, &arc)) return arc.Radius();
    }
    return std::nullopt;
  }
  void Build(CommandContext& ctx) {
    Constraint c;
    c.type = type_;
    c.value = value_;
    Document& doc = ctx.Doc();
    bool ok = true;
    switch (type_) {
      case ConstraintType::Coincident:
      case ConstraintType::Distance: {
        PointRef a, b;
        ok = ClosestPair(doc, ids_[0], ids_[1], a, b);
        c.points = {a, b};
        break;
      }
      case ConstraintType::Horizontal:
      case ConstraintType::Vertical: {
        PointRef a, b;
        const SceneObject* o = doc.Find(ids_[0]);
        ok = o && LineEndpoints(*o, a, b);
        c.points = {a, b};
        break;
      }
      case ConstraintType::Parallel:
      case ConstraintType::Perpendicular:
      case ConstraintType::EqualLength:
      case ConstraintType::Angle: {
        PointRef a0, a1, b0, b1;
        const SceneObject *oa = doc.Find(ids_[0]), *ob = doc.Find(ids_[1]);
        ok = oa && ob && LineEndpoints(*oa, a0, a1) && LineEndpoints(*ob, b0, b1);
        c.points = {a0, a1, b0, b1};
        break;
      }
      case ConstraintType::Tangent: {
        const SceneObject *o0 = doc.Find(ids_[0]), *o1 = doc.Find(ids_[1]);
        ok = o0 && o1;
        if (ok) {
          const bool c0 = IsCircleOrArc(*o0);
          const SceneObject* circle_obj = c0 ? o0 : o1;
          const SceneObject* line_obj = c0 ? o1 : o0;
          PointRef p0, p1;
          ok = IsCircleOrArc(*circle_obj) && LineEndpoints(*line_obj, p0, p1);
          if (ok) { c.points = {{circle_obj->id, -1}, p0, p1}; c.radius_objects = {circle_obj->id}; }
        }
        break;
      }
      case ConstraintType::EqualRadius: {
        const SceneObject *o0 = doc.Find(ids_[0]), *o1 = doc.Find(ids_[1]);
        ok = o0 && o1 && IsCircleOrArc(*o0) && IsCircleOrArc(*o1);
        if (ok) c.radius_objects = {o0->id, o1->id};
        break;
      }
      case ConstraintType::Radius: {
        const SceneObject* o0 = doc.Find(ids_[0]);
        ok = o0 && IsCircleOrArc(*o0);
        if (ok) c.radius_objects = {o0->id};
        break;
      }
      case ConstraintType::Fixed: {
        const SceneObject* o0 = doc.Find(ids_[0]);
        ok = o0 != nullptr;
        if (ok) {
          std::vector<PointRef> cand = CandidatePoints(*o0);
          ok = !cand.empty();
          if (ok) {
            c.points = {cand[0]};
            RefWorld(doc, cand[0], c.fixed_target);
          }
        }
        break;
      }
      case ConstraintType::Midpoint: {
        const SceneObject *pt_obj = doc.Find(ids_[0]), *seg_obj = doc.Find(ids_[1]);
        PointRef b0, b1;
        ok = pt_obj && seg_obj && LineEndpoints(*seg_obj, b0, b1);
        if (ok) {
          Point3d wb0, wb1;
          RefWorld(doc, b0, wb0);
          RefWorld(doc, b1, wb1);
          Point3d mid = (wb0 + wb1) / 2.0;
          double best = -1;
          PointRef chosen;
          for (const PointRef& p : CandidatePoints(*pt_obj)) {
            Point3d w;
            if (!RefWorld(doc, p, w)) continue;
            double d = (w - mid).Length();
            if (best < 0 || d < best) { best = d; chosen = p; }
          }
          ok = best >= 0;
          c.points = {chosen, b0, b1};
        }
        break;
      }
      case ConstraintType::Symmetric: {
        const SceneObject *axis_obj = doc.Find(ids_[0]), *oa = doc.Find(ids_[1]), *ob = doc.Find(ids_[2]);
        PointRef ax0, ax1;
        ok = axis_obj && oa && ob && LineEndpoints(*axis_obj, ax0, ax1);
        if (ok) {
          std::vector<PointRef> ca = CandidatePoints(*oa), cb = CandidatePoints(*ob);
          ok = !ca.empty() && !cb.empty();
          if (ok) c.points = {ca[0], cb[0], ax0, ax1};
        }
        break;
      }
    }
    if (!ok) { ctx.Warn("Could not build a " + std::string(sketch::ConstraintTypeName(type_)) + " constraint from the selected geometry."); Finish(); return; }
    doc.BeginChange("Constrain");
    int id = sketch::AddConstraint(doc, c);
    ctx.Print(sketch::ConstraintTypeName(type_) + std::string(" constraint #") + std::to_string(id) + " added.");
    std::vector<std::string> report;
    sketch::SolveAll(doc, ActiveCPlane(ctx), report);
    Finish();
  }

  ConstraintType type_ = ConstraintType::Perpendicular;
  std::vector<ObjectId> ids_;
  double value_ = 0;
};

class ConstraintSolveCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    std::vector<std::string> report;
    bool ok = sketch::SolveAll(ctx.Doc(), ActiveCPlane(ctx), report);
    for (const std::string& line : report) ctx.Print(line);
    ctx.Print(std::string("ConstraintSolve: ") + (ok ? "converged" : "did not fully converge"));
    Finish();
  }
};

class ConstraintDeleteCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantText("Constraint id to delete (or All)"); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (ToLower(t) == "all") {
      for (const Constraint& c : sketch::LoadConstraints(ctx.Doc())) sketch::DeleteConstraint(ctx.Doc(), c.id);
      ctx.Print("ConstraintDelete: removed every constraint.");
      Finish();
      return;
    }
    int id = std::atoi(t.c_str());
    if (sketch::DeleteConstraint(ctx.Doc(), id)) ctx.Print("ConstraintDelete: removed constraint #" + std::to_string(id) + ".");
    else ctx.Warn("No constraint #" + std::to_string(id));
    Finish();
  }
  void OnNumber(CommandContext& ctx, double v) override { OnText(ctx, std::to_string(static_cast<int>(v))); }
};

// Toggle drawn as a persistent overlay from ConstraintsFrame() (below),
// since a command finishes (and clears its own preview) long before the
// next rendered frame - only a per-frame hook can keep glyphs on screen.
bool g_show_glyphs = false;

class ConstraintsShowCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    g_show_glyphs = !g_show_glyphs;
    std::vector<Constraint> list = sketch::LoadConstraints(ctx.Doc());
    ctx.Print(std::string("ConstraintsShow: glyphs ") + (g_show_glyphs ? "on" : "off") + " (" + std::to_string(list.size()) + " constraint(s))");
    for (const Constraint& c : list) ctx.Print("  #" + std::to_string(c.id) + " " + sketch::ConstraintTypeName(c.type));
    Finish();
  }
};

}  // namespace

// Called every Application::Frame() (see Application.cpp): re-solves any
// constrained curve a plain Move/gumball drag just displaced, and keeps
// the ConstraintsShow overlay's glyph lines in the shared preview buffer
// up to date without leaking previous frames' lines into it.
void ConstraintsFrame(Application& app) {
  sketch::AutoResolveFrame(app);
  static size_t last_glyph_floats = 0;
  std::vector<float>& lines = app.Engine().PreviewLines();
  if (last_glyph_floats <= lines.size()) lines.resize(lines.size() - last_glyph_floats);
  last_glyph_floats = 0;
  if (!g_show_glyphs || app.Engine().IsRunning()) return;
  if (Viewport* vp = app.ActiveViewport()) {
    std::vector<float> glyphs = sketch::BuildGlyphs(app.Doc(), vp->CPlane());
    lines.insert(lines.end(), glyphs.begin(), glyphs.end());
    last_glyph_floats = glyphs.size();
  }
}

void RegisterConstraintCommands(CommandEngine& e) {
  Reg(e, "Constrain", Make<ConstrainCommand>());
  Reg(e, "ConstraintSolve", Make<ConstraintSolveCommand>());
  Reg(e, "ConstraintDelete", Make<ConstraintDeleteCommand>());
  Reg(e, "ConstraintsShow", Make<ConstraintsShowCommand>());
  Reg(e, "ConstraintsPanel", Immediate([](CommandContext& ctx) {
        std::vector<Constraint> list = sketch::LoadConstraints(ctx.Doc());
        ctx.Print("Constraints (" + std::to_string(list.size()) + "):");
        for (const Constraint& c : list) ctx.Print("  #" + std::to_string(c.id) + " " + sketch::ConstraintTypeName(c.type));
      }), CommandStatus::Implemented, "Prints the Constraints panel's contents to the command history.");
}

}  // namespace dino8::app
