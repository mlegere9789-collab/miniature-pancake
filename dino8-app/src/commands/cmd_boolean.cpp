// Boolean and splitting commands (mesh-based, via Manifold).
#include "commands/cmd_common.h"

namespace dino8::app {

namespace {

// Shared by BooleanCommand and Boolean2ObjectsCommand: unions each side's
// own set (when there are several objects per side), then combines the two
// sides with `op`. `swap_sides` runs the op with the sides reversed, so
// e.g. Difference(A,B) can be flipped to Difference(B,A) without the caller
// re-collecting meshes.
void RunBoolean(CommandContext& ctx, const std::vector<ObjectId>& a, const std::vector<ObjectId>& b, kernel::BooleanOp op,
                bool two_sets, const std::string& label, bool swap_sides = false) {
  std::vector<std::pair<ObjectId, kernel::Mesh>> ma, mb;
  auto collect = [&](const std::vector<ObjectId>& ids, std::vector<std::pair<ObjectId, kernel::Mesh>>& out) {
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, AdaptiveMeshTolerance(*o));
      if (!m || !m->IsClosedManifold()) { ctx.Warn("Object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
      out.push_back({id, *m});
    }
  };
  std::vector<ObjectId> all = a;
  all.insert(all.end(), b.begin(), b.end());
  if (two_sets) { collect(a, ma); collect(b, mb); }
  else { collect(all, ma); }
  if (ma.empty() || (two_sets && mb.empty())) { ctx.Warn("Nothing to combine"); return; }
  try {
    kernel::Mesh result = ma[0].second;
    int layer = ctx.Doc().Find(ma[0].first) ? ctx.Doc().Find(ma[0].first)->layer_index : 0;
    if (two_sets) {
      for (size_t i = 1; i < ma.size(); ++i) result = kernel::BooleanCombine(result, ma[i].second, kernel::BooleanOp::Union);
      kernel::Mesh other = mb[0].second;
      for (size_t i = 1; i < mb.size(); ++i) other = kernel::BooleanCombine(other, mb[i].second, kernel::BooleanOp::Union);
      result = swap_sides ? kernel::BooleanCombine(other, result, op) : kernel::BooleanCombine(result, other, op);
    } else {
      for (size_t i = 1; i < ma.size(); ++i) result = kernel::BooleanCombine(result, ma[i].second, op);
    }
    ctx.Doc().BeginChange(label);
    for (auto& [id, m] : ma) ctx.Doc().Remove(id);
    for (auto& [id, m] : mb) ctx.Doc().Remove(id);
    if (result.FaceCount() > 0) {
      SceneObject n = SceneObject::MakeMesh(result);
      n.layer_index = layer;
      ctx.Doc().Add(std::move(n));
      ctx.Print(label + ": " + std::to_string(result.FaceCount()) + " faces, volume " + FormatNumber(result.Volume()));
    } else {
      ctx.Print(label + ": result is empty");
    }
  } catch (const std::exception& ex) {
    ctx.Warn(std::string("Boolean failed: ") + ex.what());
  }
}

// Two-set boolean: first selection, then second selection.
class BooleanCommand : public Command {
 public:
  BooleanCommand(kernel::BooleanOp op, const char* label, bool two_sets) : op_(op), label_(label), two_sets_(two_sets) {}
  void Begin(CommandContext&) override { WantObjects(two_sets_ ? std::string("Select first set of objects") : "Select objects to " + std::string(label_)); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (two_sets_ && a_.empty()) {
      a_ = ids;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      WantObjects("Select second set of objects");
      accept_preselection = false;
      return;
    }
    RunBoolean(ctx, a_, ids, op_, two_sets_, label_);
    Finish();
  }
  kernel::BooleanOp op_;
  const char* label_;
  bool two_sets_;
  std::vector<ObjectId> a_;
};

// Boolean2Objects: like BooleanDifference/Intersection/Union but for exactly
// two sets, with a "Result" option that cycles through every combination
// (Union / Intersection / A-B / B-A / SymmetricDifference) before
// committing on Enter - Rhino's own way of letting you preview and pick the
// result you want without a separate command per combination.
class Boolean2ObjectsCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select first set of objects"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (a_.empty()) {
      a_ = ids;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      WantObjects("Select second set of objects");
      accept_preselection = false;
      return;
    }
    b_ = ids;
    ready_ = true;
    options = {{"Result", result_, {"Union", "Intersection", "A-B", "B-A", "SymmetricDifference"}, false, false}};
    WantEnter("Press Enter to combine (Result=" + result_ + ")");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n != "Result") return;
    result_ = v;
    options[0].value = v;
    prompt = "Press Enter to combine (Result=" + result_ + ")";
  }
  void OnEnter(CommandContext& ctx) override { if (ready_) Run(ctx); }
  // A stray non-Enter token while picking the first/second set (`ready_`
  // still false) is not an accept-with-defaults - falling through to
  // Run() here would combine with whichever set is still empty and eat
  // the token besides. Only once both sets are in (the "Result=" confirm
  // prompt) does an unrecognized token commit with the current Result
  // rather than sit modal; it is then re-run as its own command.
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (!ready_) return;
    Run(ctx);
    ctx.Engine().Execute(t);
  }
  void Run(CommandContext& ctx) {
    kernel::BooleanOp op = kernel::BooleanOp::Union;
    bool swap = false;
    if (result_ == "Intersection") op = kernel::BooleanOp::Intersection;
    else if (result_ == "A-B") op = kernel::BooleanOp::Difference;
    else if (result_ == "B-A") { op = kernel::BooleanOp::Difference; swap = true; }
    else if (result_ == "SymmetricDifference") op = kernel::BooleanOp::SymmetricDifference;
    RunBoolean(ctx, a_, b_, op, true, "Boolean2Objects", swap);
    Finish();
  }
  std::vector<ObjectId> a_, b_;
  std::string result_ = "Union";
  bool ready_ = false;
};

// Split solids by a plane through two picked points (normal to the CPlane).
class SplitPlaneCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select solids to split"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantPoint("Start of cutting line"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!a_) { a_ = p; WantPoint("End of cutting line"); return; }
    Vector3d n = ON_CrossProduct(p - *a_, ActiveNormal(ctx));
    if (n.Length() <= 0) { ctx.Warn("Degenerate cutting line"); Finish(); return; }
    n.Unitize();
    const double offset = ON_DotProduct(n, *a_);
    ctx.Doc().BeginChange("Split");
    int made = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, AdaptiveMeshTolerance(*o));
      if (!m || !m->IsClosedManifold()) { ctx.Warn("Object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
      try {
        auto [pos, neg] = kernel::SplitByPlane(*m, n, offset);
        const int layer = o->layer_index;
        ctx.Doc().Remove(id);
        for (kernel::Mesh* part : {&pos, &neg}) if (part->FaceCount() > 0) { SceneObject s = SceneObject::MakeMesh(*part); s.layer_index = layer; ctx.Doc().Add(std::move(s)); ++made; }
      } catch (const std::exception& ex) { ctx.Warn(ex.what()); }
    }
    ctx.Print("Split into " + std::to_string(made) + " piece(s)");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (a_) { ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> a_;
};

// WireCut: cuts by a plane through two picked points, like Split, but keeps
// only one side (the "Side" option) and discards the other - unlike
// BooleanSplit/MeshSplit/MeshBooleanSplit, which keep both pieces.
class WireCutCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Side", "Positive", {"Positive", "Negative"}, false, false}};
    WantObjects("Select solids to cut");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Side") { keep_positive_ = (v == "Positive"); options[0].value = v; } }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantPoint("Start of cutting line"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!a_) { a_ = p; WantPoint("End of cutting line"); return; }
    Vector3d n = ON_CrossProduct(p - *a_, ActiveNormal(ctx));
    if (n.Length() <= 0) { ctx.Warn("Degenerate cutting line"); Finish(); return; }
    n.Unitize();
    const double offset = ON_DotProduct(n, *a_);
    ctx.Doc().BeginChange("WireCut");
    int made = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, AdaptiveMeshTolerance(*o));
      if (!m || !m->IsClosedManifold()) { ctx.Warn("Object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
      try {
        auto [pos, neg] = kernel::SplitByPlane(*m, n, offset);
        const int layer = o->layer_index;
        ctx.Doc().Remove(id);
        kernel::Mesh& keep = keep_positive_ ? pos : neg;
        if (keep.FaceCount() > 0) { SceneObject s = SceneObject::MakeMesh(keep); s.layer_index = layer; ctx.Doc().Add(std::move(s)); ++made; }
      } catch (const std::exception& ex) { ctx.Warn(ex.what()); }
    }
    ctx.Print("WireCut: kept " + std::to_string(made) + " piece(s), discarded the other side");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (a_) { ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> a_;
  bool keep_positive_ = true;
};

// Turns an open cutter mesh (a surface tessellation, an open mesh - one
// simply-connected patch with a single boundary loop) into a closed
// half-space-like solid BooleanCombine can use: extrudes it, starting
// exactly at its own real position, along its own area-weighted average
// face normal far enough to clear every target in `span` (the targets'
// combined bounding box) - so BooleanCombine(target, this solid,
// Intersection)/Difference split the target into the piece on the
// outward-normal side of the cutter and the piece on the other side,
// the same halves kernel::SplitByPlane's own exact plane cutter produces
// for a flat cutter, approximated here for a general (possibly curved)
// one. Extruding from the cap's OWN position (not shifted back first) is
// what makes this a half-space rather than a thickened slab centered on
// the cutter - a slab wide enough to clear the target from both sides
// would swallow it whole (Intersection = the entire target, Difference =
// empty) instead of splitting it in two. Only an approximation for a
// strongly curved cutter (the extrusion direction is one fixed vector,
// not a per-point normal offset) - the same honest tradeoff RegionSlab/
// SlabFromTrimmedPlane and OffsetSrf Solid=Yes already accept elsewhere
// in this codebase (cmd_solidtools.cpp, cmd_surface.cpp) for turning an
// open cap into a solid. Returns nullopt if `cap` has no faces, is
// degenerate (zero net normal, e.g. a folded or self-cancelling patch)
// or ExtrudeCappedSolid rejects it (no clean single boundary loop).
std::optional<kernel::Mesh> SolidifyOpenCutter(const kernel::Mesh& cap, const kernel::BoundingBox& span) {
  if (cap.FaceCount() == 0) return std::nullopt;
  const ON_Mesh& raw = cap.raw();
  ON_3dVector normal_sum(0, 0, 0);
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& f = raw.m_F[i];
    const ON_3fPoint& a = raw.m_V[f.vi[0]];
    const ON_3fPoint& b = raw.m_V[f.vi[1]];
    const ON_3fPoint& c = raw.m_V[f.vi[2]];
    normal_sum += ON_CrossProduct(ON_3dVector(b - a), ON_3dVector(c - a));
    if (f.IsQuad()) {
      const ON_3fPoint& d = raw.m_V[f.vi[3]];
      normal_sum += ON_CrossProduct(ON_3dVector(c - a), ON_3dVector(d - a));
    }
  }
  if (normal_sum.Length() <= 0) return std::nullopt;
  normal_sum.Unitize();
  const kernel::BoundingBox cap_bb = cap.GetBoundingBox();
  kernel::BoundingBox combined = span;
  combined.min.x = std::min(combined.min.x, cap_bb.min.x); combined.min.y = std::min(combined.min.y, cap_bb.min.y); combined.min.z = std::min(combined.min.z, cap_bb.min.z);
  combined.max.x = std::max(combined.max.x, cap_bb.max.x); combined.max.y = std::max(combined.max.y, cap_bb.max.y); combined.max.z = std::max(combined.max.z, cap_bb.max.z);
  const double reach = std::max((combined.max - combined.min).Length(), 1.0) * 2.0;
  try {
    kernel::Mesh solid = kernel::Mesh::ExtrudeCappedSolid(cap, normal_sum * reach);
    if (solid.FaceCount() == 0 || !solid.IsClosedManifold()) return std::nullopt;
    if (solid.Volume() < 0) solid = solid.FlipNormals();
    return solid;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// SplitByObject: splits solids by one or more arbitrary cutting objects
// (other solids, or open surfaces/meshes), keeping both resulting pieces -
// the general cutting-object case that BooleanSplit/MeshSplit/
// MeshBooleanSplit above don't cover (those only cut by a plane through two
// picked points; see PARITY_MAP.md kernel:features "Split body with an
// arbitrary surface / solid cutter"). Every cutting object already a
// closed solid is used as-is; an open one is solidified first
// (SolidifyOpenCutter above), then every cutter is unioned into one, the
// same "merge all cutters into a single rigid tool" approach HoleArray
// (cmd_solidtools.cpp) uses for multiple hole centres. Each target then
// gets cut by that one merged tool: BooleanCombine(target, tool,
// Intersection) is the piece inside the cutter, BooleanCombine(target,
// tool, Difference) is the piece outside it - either can come back empty
// (the cutter missing the target entirely, or fully containing it), in
// which case only the non-empty piece is kept.
class SplitByObjectCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select solids to split"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!have_targets_) {
      target_ids_ = ids;
      have_targets_ = true;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      WantObjects("Select cutting objects (solids or surfaces)");
      accept_preselection = false;
      return;
    }
    Run(ctx, ids);
    Finish();
  }
  void Run(CommandContext& ctx, const std::vector<ObjectId>& cutter_ids) {
    std::vector<std::pair<ObjectId, kernel::Mesh>> targets;
    kernel::BoundingBox span;
    bool have_span = false;
    for (ObjectId id : target_ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, AdaptiveMeshTolerance(*o));
      if (!m || !m->IsClosedManifold()) { ctx.Warn("SplitByObject: object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
      const kernel::BoundingBox bb = m->GetBoundingBox();
      if (!have_span) { span = bb; have_span = true; }
      else {
        span.min.x = std::min(span.min.x, bb.min.x); span.min.y = std::min(span.min.y, bb.min.y); span.min.z = std::min(span.min.z, bb.min.z);
        span.max.x = std::max(span.max.x, bb.max.x); span.max.y = std::max(span.max.y, bb.max.y); span.max.z = std::max(span.max.z, bb.max.z);
      }
      targets.push_back({id, *m});
    }
    if (targets.empty()) { ctx.Warn("SplitByObject: no closed solids to split"); return; }

    std::optional<kernel::Mesh> tool;
    std::vector<ObjectId> used_cutter_ids;
    for (ObjectId id : cutter_ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> cm = MeshOf(*o, AdaptiveMeshTolerance(*o));
      if (!cm || cm->FaceCount() == 0) continue;
      std::optional<kernel::Mesh> solidified = cm->IsClosedManifold() ? cm : SolidifyOpenCutter(*cm, span);
      if (!solidified) { ctx.Warn("SplitByObject: cutting object " + std::to_string(id) + " could not be turned into a cutting solid; skipped"); continue; }
      used_cutter_ids.push_back(id);
      if (!tool) { tool = solidified; continue; }
      try { tool = kernel::BooleanCombine(*tool, *solidified, kernel::BooleanOp::Union); }
      catch (const std::exception&) { /* keep the previous tool; one bad cutter shouldn't lose the rest */ }
    }
    if (!tool) { ctx.Warn("SplitByObject: no usable cutting objects"); return; }

    ctx.Doc().BeginChange("SplitByObject");
    // The cutting object(s) are consumed into the split, same as
    // BooleanDifference/BooleanIntersection consume both of their operands
    // (RunBoolean above) - not left behind as leftover geometry.
    for (ObjectId id : used_cutter_ids) ctx.Doc().Remove(id);
    int cut = 0, made = 0;
    for (auto& [id, mesh] : targets) {
      const SceneObject* o = ctx.Doc().Find(id);
      const int layer = o ? o->layer_index : 0;
      std::optional<kernel::Mesh> inside, outside;
      try { kernel::Mesh r = kernel::BooleanCombine(mesh, *tool, kernel::BooleanOp::Intersection); if (r.FaceCount() > 0) inside = r; }
      catch (const std::exception&) {}
      try { kernel::Mesh r = kernel::BooleanCombine(mesh, *tool, kernel::BooleanOp::Difference); if (r.FaceCount() > 0) outside = r; }
      catch (const std::exception&) {}
      if (!inside && !outside) { ctx.Warn("SplitByObject: object " + std::to_string(id) + " does not intersect the cutting object(s)"); continue; }
      ctx.Doc().Remove(id);
      for (std::optional<kernel::Mesh>* piece : {&inside, &outside}) {
        if (!*piece) continue;
        SceneObject s = SceneObject::MakeMesh(**piece);
        s.layer_index = layer;
        ctx.Doc().Add(std::move(s));
        ++made;
      }
      ++cut;
    }
    ctx.Print("SplitByObject: " + std::to_string(cut) + " solid(s) split into " + std::to_string(made) + " piece(s) (mesh boolean; results are meshes)");
  }
  std::vector<ObjectId> target_ids_;
  bool have_targets_ = false;
};

// MeshSmooth: smooths and refines with typed Strength (0-1, softens creases)
// and MinSharpAngle options and a computed default target edge length,
// unlike the previous fixed-parameter pass.
class MeshSmoothCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Strength", "0", {}, true, false}, {"MinSharpAngle", "52.5", {}, true, false}};
    WantObjects("Select meshes to smooth");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    char* e = nullptr;
    double d = std::strtod(v.c_str(), &e);
    if (!e || *e) return;
    if (n == "Strength") { strength_ = std::clamp(d, 0.0, 1.0); for (OptionSpec& o : options) if (o.name == "Strength") o.value = FormatNumber(strength_); }
    else if (n == "MinSharpAngle") { min_sharp_angle_ = d; for (OptionSpec& o : options) if (o.name == "MinSharpAngle") o.value = FormatNumber(min_sharp_angle_); }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ctx.Doc().BeginChange("MeshSmooth");
    int done = 0;
    for (ObjectId id : ids) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind != ObjectKind::Mesh) continue;
      try {
        kernel::BoundingBox bb = o->mesh->GetBoundingBox();
        double len = (bb.max - bb.min).Length() / 30;
        *o->mesh = kernel::SmoothAndRefine(*o->mesh, len, min_sharp_angle_, strength_);
        o->InvalidateDisplay();
        ++done;
      } catch (const std::exception& ex) { ctx.Warn(ex.what()); }
    }
    ctx.Print("MeshSmooth: " + std::to_string(done) + " mesh(es) (Strength=" + FormatNumber(strength_) + " MinSharpAngle=" + FormatNumber(min_sharp_angle_) + ")");
    Finish();
  }
  double strength_ = 0.0;
  double min_sharp_angle_ = 52.5;
};

}  // namespace

void RegisterBooleanCommands(CommandEngine& e) {
  Reg(e, "BooleanUnion", Make<BooleanCommand>(kernel::BooleanOp::Union, "BooleanUnion", false));
  Reg(e, "BooleanDifference", Make<BooleanCommand>(kernel::BooleanOp::Difference, "BooleanDifference", true));
  Reg(e, "BooleanIntersection", Make<BooleanCommand>(kernel::BooleanOp::Intersection, "BooleanIntersection", true));
  Reg(e, "Boolean2Objects", Make<Boolean2ObjectsCommand>());
  Reg(e, "MeshBooleanUnion", Make<BooleanCommand>(kernel::BooleanOp::Union, "MeshBooleanUnion", false));
  Reg(e, "MeshBooleanDifference", Make<BooleanCommand>(kernel::BooleanOp::Difference, "MeshBooleanDifference", true));
  Reg(e, "MeshBooleanIntersection", Make<BooleanCommand>(kernel::BooleanOp::Intersection, "MeshBooleanIntersection", true));
  // "Split" itself lives in cmd_curveedit.cpp (curves by curves); it delegates solids to BooleanSplit.
  Reg(e, "BooleanSplit", Make<SplitPlaneCommand>());
  Reg(e, "MeshSplit", Make<SplitPlaneCommand>());
  Reg(e, "MeshBooleanSplit", Make<SplitPlaneCommand>());
  Reg(e, "SplitByObject", Make<SplitByObjectCommand>(), CommandStatus::Implemented,
      "Splits solids by one or more cutting solids or open surfaces (extended into a solid), keeping both resulting pieces - the general cutting-object BooleanSplit that BooleanSplit itself (a plane through two points only) does not cover.");
  Reg(e, "WireCut", Make<WireCutCommand>());
  // ReduceMesh: superseded, dead code (RegisterRemeshCommands registers the
  // real target-count-driven ReduceMesh afterwards; see
  // Application::RegisterCommands).
  Reg(e, "MeshSmooth", Make<MeshSmoothCommand>());
  Reg(e, "SplitDisjointMesh", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("SplitDisjointMesh");
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o || o->kind != ObjectKind::Mesh) continue; std::vector<kernel::Mesh> parts = kernel::Decompose(*o->mesh); if (parts.size() < 2) continue; int layer = o->layer_index; ctx.Doc().Remove(id); for (const kernel::Mesh& p : parts) { SceneObject s = SceneObject::MakeMesh(p); s.layer_index = layer; ctx.Doc().Add(std::move(s)); } }
      }));
  Reg(e, "Weld", OnSelection("Select meshes to weld", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Weld");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { *o->mesh = kernel::Mesh::MergeAndWeld({*o->mesh}, ctx.Settings().absolute_tolerance); o->InvalidateDisplay(); } }
      }));
  Reg(e, "UnifyMeshNormals", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("UnifyMeshNormals");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { o->mesh->raw().ComputeFaceNormals(); o->mesh->raw().ComputeVertexNormals(); o->InvalidateDisplay(); } }
      }));
  Reg(e, "RebuildMeshNormals", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { o->mesh->raw().ComputeFaceNormals(); o->mesh->raw().ComputeVertexNormals(); o->InvalidateDisplay(); } }
      }));
  Reg(e, "TriangulateMesh", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("TriangulateMesh");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { o->mesh->raw().ConvertQuadsToTriangles(); o->InvalidateDisplay(); } }
      }));
  Reg(e, "QuadrangulateMesh", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("QuadrangulateMesh");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { o->mesh->raw().ConvertTrianglesToQuads(ON_PI / 90.0, 0.0); o->InvalidateDisplay(); } }
      }));
  Reg(e, "CullDegenerateMeshFaces", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("CullDegenerateMeshFaces");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { int n = o->mesh->raw().CullDegenerateFaces(); ctx.Print("Removed " + std::to_string(n) + " degenerate face(s)"); o->InvalidateDisplay(); } }
      }));
  Reg(e, "CheckMesh", OnSelection("Select meshes to check", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Mesh) continue;
          const bool closed = o->mesh->IsClosedManifold();
          ctx.Print("Mesh " + std::to_string(id) + ": " + std::to_string(o->mesh->VertexCount()) + " vertices, " + std::to_string(o->mesh->FaceCount()) + " faces, " + (closed ? "closed manifold" : "open or non-manifold"));
          if (closed) {
            try { ctx.Print("  degenerate triangles: " + std::to_string(kernel::CountDegenerateTriangles(*o->mesh))); }
            catch (const std::exception& ex) { ctx.Warn(ex.what()); }
          }
        }
      }));
  Reg(e, "ConvexHull", OnSelection("Select objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<Point3d> pts;
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o) continue; if (o->kind == ObjectKind::Point) pts.push_back(o->point); else { o->EnsureDisplay(0.05, 0.1); const DisplayCache& d = o->Display(); for (size_t i = 0; i + 2 < d.lines.size(); i += 3) pts.emplace_back(d.lines[i], d.lines[i + 1], d.lines[i + 2]); for (size_t i = 0; i + 5 < d.triangles.size(); i += 6) pts.emplace_back(d.triangles[i], d.triangles[i + 1], d.triangles[i + 2]); } }
        if (pts.size() < 4) { ctx.Warn("Need at least four points"); return; }
        try { AddObject(ctx, SceneObject::MakeMesh(kernel::ConvexHull(pts)), "ConvexHull"); } catch (const std::exception& ex) { ctx.Warn(ex.what()); }
      }));
}

}  // namespace dino8::app
