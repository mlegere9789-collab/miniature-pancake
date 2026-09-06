// Drafting helpers: Hatch, Make2D, and Blocks (definitions + instances).
#include "commands/cmd_common.h"

#include <algorithm>

namespace dino8::app {

namespace {

// Moller-Trumbore ray/triangle test; out_t is the ray parameter of the hit.
bool Make2DRayTriangle(Point3d o, Vector3d d, Point3d t0, Point3d t1, Point3d t2, double& out_t) {
  const Vector3d e1 = t1 - t0, e2 = t2 - t0;
  const Vector3d p = ON_CrossProduct(d, e2);
  const double det = ON_DotProduct(e1, p);
  if (std::fabs(det) < 1e-12) return false;
  const double inv = 1 / det;
  const Vector3d s = o - t0;
  const double u = ON_DotProduct(s, p) * inv;
  if (u < 0 || u > 1) return false;
  const Vector3d q = ON_CrossProduct(s, e1);
  const double v = ON_DotProduct(d, q) * inv;
  if (v < 0 || u + v > 1) return false;
  out_t = ON_DotProduct(e2, q) * inv;
  return true;
}

// Make2D: project the selection's display lines through the active view onto
// the CPlane, splitting each into visible / hidden runs by ray-testing its
// midpoint (in 3D, before projection) against every visible object's mesh.
void Make2D(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  Viewport* vp = ctx.ActiveViewport();
  if (!vp) return;
  const ON_Plane pl = ActivePlane(ctx);
  const CameraState& cam = vp->GetCamera().State();
  const Vector3d f = vp->GetCamera().Forward();
  auto project = [&](const Point3d& p) {
    // Along the view direction onto the CPlane (parallel projection).
    const double denom = ON_DotProduct(f, pl.zaxis);
    if (std::fabs(denom) < 1e-9) return pl.ClosestPointTo(p);
    const double t = ON_DotProduct(pl.origin - p, pl.zaxis) / denom;
    return p + f * t;
  };

  // Occluder triangles: every visible object's tessellation.
  struct Tri3 { Point3d a, b, c; };
  std::vector<Tri3> occluders;
  ON_BoundingBox bb;
  bool have_bb = false;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(o)) continue;
    std::optional<kernel::Mesh> m = MeshOf(o, ctx.App().surface_display_tolerance);
    if (!m) continue;
    const ON_Mesh& raw = m->raw();
    for (int i = 0; i < raw.FaceCount(); ++i) {
      const ON_MeshFace& mf = raw.m_F[i];
      const Point3d v0 = raw.Vertex(mf.vi[0]), v1 = raw.Vertex(mf.vi[1]), v2 = raw.Vertex(mf.vi[2]);
      occluders.push_back({v0, v1, v2});
      if (mf.vi[3] != mf.vi[2]) occluders.push_back({v0, v2, raw.Vertex(mf.vi[3])});
      bb.Set(v0, true); bb.Set(v1, true); bb.Set(v2, true);
      have_bb = true;
    }
  }
  const double diag = have_bb ? bb.Diagonal().Length() : 1.0;
  const double eps = std::max(1e-6, diag * 1e-5);
  const double back = diag * 10 + 10;

  // True if `p` (a point on some object's own surface/edge) is not blocked
  // from the camera by any occluder closer than itself.
  auto Visible = [&](Point3d p) {
    Point3d origin;
    Vector3d d;
    double t_p;
    if (cam.perspective) {
      d = p - cam.eye;
      t_p = d.Length();
      if (t_p < 1e-12) return true;
      d /= t_p;
      origin = cam.eye;
    } else {
      d = f;
      origin = p - f * back;
      t_p = back;
    }
    for (const Tri3& t : occluders) {
      double tt;
      if (Make2DRayTriangle(origin, d, t.a, t.b, t.c, tt) && tt < t_p - eps) return false;
    }
    return true;
  };

  std::vector<kernel::NurbsCurve> visible_curves, hidden_curves;
  int visible_segs = 0, hidden_segs = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    o->EnsureDisplay(ctx.App().curve_display_tolerance, ctx.App().surface_display_tolerance);
    const DisplayCache& d = o->Display();
    // Chain consecutive same-visibility segments into polylines.
    std::vector<Point3d> run;
    bool run_visible = true;
    auto flush = [&]() {
      if (run.size() >= 2) {
        (run_visible ? visible_curves : hidden_curves).push_back(PolylineCurve(run));
        (run_visible ? visible_segs : hidden_segs) += static_cast<int>(run.size()) - 1;
      }
      run.clear();
    };
    for (size_t i = 0; i + 5 < d.lines.size(); i += 6) {
      const Point3d a3(d.lines[i], d.lines[i + 1], d.lines[i + 2]);
      const Point3d b3(d.lines[i + 3], d.lines[i + 4], d.lines[i + 5]);
      const bool seg_visible = Visible(Point3d((a3.x + b3.x) / 2, (a3.y + b3.y) / 2, (a3.z + b3.z) / 2));
      const Point3d a = project(a3), b = project(b3);
      if (run.empty()) { run_visible = seg_visible; run = {a, b}; continue; }
      if (seg_visible == run_visible && run.back().DistanceTo(a) <= 1e-9) { run.push_back(b); continue; }
      flush();
      run_visible = seg_visible;
      run = {a, b};
    }
    flush();
  }
  if (visible_curves.empty() && hidden_curves.empty()) { ctx.Warn("Nothing to project"); return; }
  ctx.Doc().BeginChange("Make2D");
  std::vector<ObjectId> out;
  const int vis_layer = ctx.Doc().AddLayer("Make2D");
  for (const kernel::NurbsCurve& c : visible_curves) { SceneObject s = SceneObject::MakeCurve(c); s.layer_index = vis_layer; out.push_back(ctx.Doc().Add(std::move(s))); }
  if (!hidden_curves.empty()) {
    const int hid_layer = ctx.Doc().AddLayer("Make2D::Hidden");
    for (const kernel::NurbsCurve& c : hidden_curves) {
      SceneObject s = SceneObject::MakeCurve(c);
      s.layer_index = hid_layer;
      s.linetype = "Dashed";
      out.push_back(ctx.Doc().Add(std::move(s)));
    }
  }
  ctx.Doc().CreateGroup(out, "Make2D");
  ctx.Print("Make2D: " + std::to_string(visible_curves.size()) + " visible and " + std::to_string(hidden_curves.size()) +
            " hidden curve(s) (" + std::to_string(visible_segs + hidden_segs) + " segment(s), " + std::to_string(hidden_segs) +
            " hidden by occlusion)");
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
  // Hatch: superseded, dead code - RegisterDrafting2Commands re-registers
  // "Hatch" against the real pattern library (LibraryHatchCommand); a
  // registration here would only be overwritten.
  Reg(e, "Make2D", OnSelection("Select objects to draw in 2D", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Make2D(ctx, ids); }), CommandStatus::Implemented,
      "Projects visible wire geometry onto the CPlane, ray-testing each segment's midpoint against every visible object's mesh to split it into a Make2D visible curve or a Make2D::Hidden dashed one.");
  Reg(e, "Block", Make<BlockCommand>(), CommandStatus::Implemented,
      "Stores the geometry as a named BlockDefinition on the document and replaces the selection with a grouped, "
      "tagged instance (see InstantiateBlock); each instance is a transformed copy rather than a live GPU reference, "
      "but redefining the block (BlockEdit, AddObjectsToBlock) reinstantiates every instance of that name from the "
      "updated definition, so editing the definition does update every instance - real linked instancing, not just independent copies.");
  Reg(e, "Insert", Make<InsertCommand>(), CommandStatus::Implemented, "Inserts a copy of the named block definition at the given point, or falls back to Import when no blocks are defined.");
  Reg(e, "ExplodeBlock", OnSelection("Select block instances to explode", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ctx.Doc().BeginChange("ExplodeBlock"); ctx.Doc().Ungroup(ids); for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) { o->user_text.erase("Block"); o->user_text.erase("BlockInsert"); } }));
  Reg(e, "BlockManager", Immediate([](CommandContext& ctx) {
        if (ctx.Doc().Blocks().empty()) { ctx.Print("No block definitions. Use Block to create one."); return; }
        for (const BlockDefinition& b : ctx.Doc().Blocks()) {
          int instances = 0;
          for (const SceneObject& o : ctx.Doc().Objects()) { auto it = o.user_text.find("Block"); if (it != o.user_text.end() && it->second == b.name) ++instances; }
          ctx.Print("Block '" + b.name + "': " + std::to_string(b.objects.size()) + " object(s), base " + FormatPoint(b.base) + ", " + std::to_string(instances) + " object(s) in instances");
        }
        ctx.App().Panels().command_history = true;
      }), CommandStatus::Partial, "Lists every block definition and its instance count in the command history; there is no dedicated dockable panel UI for it in this build.");
  Reg(e, "SelBlockInstance", Immediate([](CommandContext& ctx) { ctx.Doc().SelectWhere([](const SceneObject& o) { return o.user_text.count("Block") > 0; }); }));
}

}  // namespace dino8::app
