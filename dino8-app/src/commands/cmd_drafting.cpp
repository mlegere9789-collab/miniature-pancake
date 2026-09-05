// Drafting helpers: Hatch, Make2D, and Blocks (definitions + instances).
#include "commands/cmd_common.h"
#include "commands/hatch_common.h"

#include <algorithm>

namespace dino8::app {

namespace {

class HatchCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    spacing_ = ctx.Settings().grid_spacing;
    WantObjects("Select closed planar curves for hatch boundaries");
    options = {{"Pattern", "Hatch1", {"Solid", "Hatch1", "Grid", "Hatch2"}, false, false}, {"Spacing", FormatNumber(spacing_), {}, true, false}, {"Rotation", "45", {}, true, false}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Pattern") { const char* pats[] = {"Solid", "Hatch1", "Grid", "Hatch2"}; pattern_ = (pattern_ + 1) % 4; options[0].value = pats[pattern_]; }
    if (n == "Spacing") { double s = std::atof(v.c_str()); if (s > 0) spacing_ = s; options[1].value = FormatNumber(spacing_); }
    if (n == "Rotation") { rotation_ = std::atof(v.c_str()); options[2].value = FormatNumber(rotation_); }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    struct Boundary { kernel::NurbsCurve curve; ObjectId id; int layer; };
    std::vector<Boundary> curves;
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve && o->curve->IsClosed()) curves.push_back({*o->curve, o->id, o->layer_index}); }
    if (curves.empty()) { ctx.Warn("Select closed planar curves"); Finish(); return; }
    ctx.Doc().BeginChange("Hatch");
    int made = 0;
    for (const Boundary& b : curves) {
      if (AddHatch(ctx, b.curve, b.id, b.layer, pattern_, spacing_, rotation_) > 0) ++made;
    }
    ctx.Print("Hatch: " + std::to_string(made) + " boundary(ies) hatched");
    Finish();
  }
  int pattern_ = 1;
  double spacing_ = 1, rotation_ = 45;
};

// Make2D: project the selection's display lines through the active view onto the CPlane.
void Make2D(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  Viewport* vp = ctx.ActiveViewport();
  if (!vp) return;
  const ON_Plane pl = ActivePlane(ctx);
  const Vector3d f = vp->GetCamera().Forward();
  auto project = [&](const Point3d& p) {
    // Along the view direction onto the CPlane (parallel projection).
    const double denom = ON_DotProduct(f, pl.zaxis);
    if (std::fabs(denom) < 1e-9) return pl.ClosestPointTo(p);
    const double t = ON_DotProduct(pl.origin - p, pl.zaxis) / denom;
    return p + f * t;
  };
  std::vector<kernel::NurbsCurve> curves;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    o->EnsureDisplay(ctx.App().curve_display_tolerance, ctx.App().surface_display_tolerance);
    const DisplayCache& d = o->Display();
    // Chain consecutive segments into polylines.
    std::vector<Point3d> run;
    for (size_t i = 0; i + 5 < d.lines.size(); i += 6) {
      const Point3d a = project(Point3d(d.lines[i], d.lines[i + 1], d.lines[i + 2]));
      const Point3d b = project(Point3d(d.lines[i + 3], d.lines[i + 4], d.lines[i + 5]));
      if (run.empty() || run.back().DistanceTo(a) > 1e-9) {
        if (run.size() >= 2) curves.push_back(PolylineCurve(run));
        run = {a};
      }
      run.push_back(b);
    }
    if (run.size() >= 2) curves.push_back(PolylineCurve(run));
  }
  if (curves.empty()) { ctx.Warn("Nothing to project"); return; }
  ctx.Doc().BeginChange("Make2D");
  std::vector<ObjectId> out;
  const int layer = ctx.Doc().AddLayer("Make2D");
  for (const kernel::NurbsCurve& c : curves) { SceneObject s = SceneObject::MakeCurve(c); s.layer_index = layer; out.push_back(ctx.Doc().Add(std::move(s))); }
  ctx.Doc().CreateGroup(out, "Make2D");
  ctx.Print("Make2D: " + std::to_string(out.size()) + " curve(s) on layer Make2D (no hidden-line removal)");
}

class BlockCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to define a block"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantPoint("Block base point"); }
  void OnPoint(CommandContext& ctx, Point3d p) override { base_ = p; WantText("Block name", "Block " + std::to_string(ctx.Doc().Blocks().size() + 1)); }
  void OnText(CommandContext& ctx, const std::string& name) override {
    ctx.Doc().BeginChange("Block");
    BlockDefinition def;
    def.name = name;
    def.base = base_;
    for (ObjectId id : ids_) if (const SceneObject* o = ctx.Doc().Find(id)) { SceneObject c = *o; c.selected = false; c.group_id = -1; c.user_text.erase("Block"); c.user_text.erase("BlockInsert"); def.objects.push_back(c); }
    if (BlockDefinition* existing = ctx.Doc().FindBlock(name)) *existing = def; else ctx.Doc().Blocks().push_back(def);
    // Replace the source objects by an instance at the same place.
    for (ObjectId id : ids_) ctx.Doc().Remove(id);
    Instantiate(ctx, name, base_);
    ctx.Print("Block '" + name + "' defined with " + std::to_string(def.objects.size()) + " object(s)");
    Finish();
  }
  static int Instantiate(CommandContext& ctx, const std::string& name, Point3d at) { return InstantiateBlock(ctx, name, at); }
  std::vector<ObjectId> ids_;
  Point3d base_;
};

class InsertCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (ctx.Doc().Blocks().empty()) {
      // No block definitions: behave like Import.
      Finish();
      ctx.Engine().Execute("Import");
      return;
    }
    std::string names;
    for (const BlockDefinition& b : ctx.Doc().Blocks()) names += (names.empty() ? "" : ", ") + b.name;
    ctx.Print("Blocks: " + names);
    WantText("Block name to insert", ctx.Doc().Blocks().back().name);
  }
  void OnText(CommandContext& ctx, const std::string& name) override {
    if (!ctx.Doc().FindBlock(name)) { ctx.Warn("No block named '" + name + "'"); Finish(); return; }
    name_ = name;
    WantPoint("Insertion point");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.Doc().BeginChange("Insert");
    BlockCommand::Instantiate(ctx, name_, p);
    ctx.Print("Inserted '" + name_ + "'. Press Enter to insert another.");
    WantPoint("Insertion point (Enter to finish)");
  }
  void OnEnter(CommandContext&) override { Finish(); }
  std::string name_;
};

}  // namespace

int InstantiateBlock(CommandContext& ctx, const std::string& name, Point3d at) {
  BlockDefinition* def = ctx.Doc().FindBlock(name);
  if (!def) return -1;
  const ON_Xform xf = ON_Xform::TranslationTransformation(at - def->base);
  std::vector<ObjectId> ids;
  for (const SceneObject& o : def->objects) {
    SceneObject c = o;
    c.id = kNoObject;
    c.selected = false;
    c.Transform(xf);
    c.user_text["Block"] = name;
    // The insertion point travels with the instance (SceneObject::Transform keeps it current).
    c.user_text["BlockInsert"] = FormatPoint(at);
    ids.push_back(ctx.Doc().Add(std::move(c)));
  }
  return ctx.Doc().CreateGroup(ids, name);
}

void RegisterDraftingCommands(CommandEngine& e) {
  Reg(e, "Hatch", Make<HatchCommand>(), CommandStatus::Partial, "Solid fills become planar surfaces; line patterns are curve groups.");
  Reg(e, "Make2D", OnSelection("Select objects to draw in 2D", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Make2D(ctx, ids); }), CommandStatus::Partial, "Projects visible wire geometry; hidden-line removal is planned.");
  Reg(e, "Block", Make<BlockCommand>(), CommandStatus::Partial, "Instances are grouped copies tagged with the block name (not linked).");
  Reg(e, "Insert", Make<InsertCommand>(), CommandStatus::Partial, "Inserts a block definition, or imports a file when none exist.");
  Reg(e, "ExplodeBlock", OnSelection("Select block instances to explode", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ctx.Doc().BeginChange("ExplodeBlock"); ctx.Doc().Ungroup(ids); for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->user_text.erase("Block"); o->user_text.erase("BlockInsert"); } }));
  Reg(e, "BlockManager", Immediate([](CommandContext& ctx) {
        if (ctx.Doc().Blocks().empty()) { ctx.Print("No block definitions. Use Block to create one."); return; }
        for (const BlockDefinition& b : ctx.Doc().Blocks()) {
          int instances = 0;
          for (const SceneObject& o : ctx.Doc().Objects()) { auto it = o.user_text.find("Block"); if (it != o.user_text.end() && it->second == b.name) ++instances; }
          ctx.Print("Block '" + b.name + "': " + std::to_string(b.objects.size()) + " object(s), base " + FormatPoint(b.base) + ", " + std::to_string(instances) + " object(s) in instances");
        }
        ctx.App().Panels().command_history = true;
      }), CommandStatus::Partial, "Lists definitions in the command history; a panel is planned.");
  Reg(e, "SelBlockInstance", Immediate([](CommandContext& ctx) { ctx.Doc().SelectWhere([](const SceneObject& o) { return o.user_text.count("Block") > 0; }); }));
}

}  // namespace dino8::app
