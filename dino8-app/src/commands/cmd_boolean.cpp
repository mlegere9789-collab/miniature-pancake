// Boolean and splitting commands (mesh-based, via Manifold).
#include "commands/cmd_common.h"

namespace dino8::app {

namespace {

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
    std::vector<ObjectId> all = a_;
    all.insert(all.end(), ids.begin(), ids.end());
    Run(ctx, a_, ids, all);
    Finish();
  }
  void Run(CommandContext& ctx, const std::vector<ObjectId>& a, const std::vector<ObjectId>& b, const std::vector<ObjectId>& all) {
    std::vector<std::pair<ObjectId, kernel::Mesh>> ma, mb;
    auto collect = [&](const std::vector<ObjectId>& ids, std::vector<std::pair<ObjectId, kernel::Mesh>>& out) {
      for (ObjectId id : ids) {
        const SceneObject* o = ctx.Doc().Find(id);
        if (!o) continue;
        std::optional<kernel::Mesh> m = MeshOf(*o, 0.005);
        if (!m || !m->IsClosedManifold()) { ctx.Warn("Object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
        out.push_back({id, *m});
      }
    };
    if (two_sets_) { collect(a, ma); collect(b, mb); }
    else { collect(all, ma); }
    if (ma.empty() || (two_sets_ && mb.empty())) { ctx.Warn("Nothing to combine"); return; }
    try {
      kernel::Mesh result = ma[0].second;
      int layer = ctx.Doc().Find(ma[0].first) ? ctx.Doc().Find(ma[0].first)->layer_index : 0;
      if (two_sets_) {
        for (size_t i = 1; i < ma.size(); ++i) result = kernel::BooleanCombine(result, ma[i].second, kernel::BooleanOp::Union);
        kernel::Mesh other = mb[0].second;
        for (size_t i = 1; i < mb.size(); ++i) other = kernel::BooleanCombine(other, mb[i].second, kernel::BooleanOp::Union);
        result = kernel::BooleanCombine(result, other, op_);
      } else {
        for (size_t i = 1; i < ma.size(); ++i) result = kernel::BooleanCombine(result, ma[i].second, op_);
      }
      ctx.Doc().BeginChange(label_);
      for (auto& [id, m] : ma) ctx.Doc().Remove(id);
      for (auto& [id, m] : mb) ctx.Doc().Remove(id);
      if (result.FaceCount() > 0) {
        SceneObject n = SceneObject::MakeMesh(result);
        n.layer_index = layer;
        ctx.Doc().Add(std::move(n));
        ctx.Print(std::string(label_) + ": " + std::to_string(result.FaceCount()) + " faces, volume " + FormatNumber(result.Volume()));
      } else {
        ctx.Print(std::string(label_) + ": result is empty");
      }
    } catch (const std::exception& ex) {
      ctx.Warn(std::string("Boolean failed: ") + ex.what());
    }
  }
  kernel::BooleanOp op_;
  const char* label_;
  bool two_sets_;
  std::vector<ObjectId> a_;
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
      std::optional<kernel::Mesh> m = MeshOf(*o, 0.005);
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

}  // namespace

void RegisterBooleanCommands(CommandEngine& e) {
  Reg(e, "BooleanUnion", Make<BooleanCommand>(kernel::BooleanOp::Union, "BooleanUnion", false));
  Reg(e, "BooleanDifference", Make<BooleanCommand>(kernel::BooleanOp::Difference, "BooleanDifference", true));
  Reg(e, "BooleanIntersection", Make<BooleanCommand>(kernel::BooleanOp::Intersection, "BooleanIntersection", true));
  Reg(e, "Boolean2Objects", Make<BooleanCommand>(kernel::BooleanOp::SymmetricDifference, "Boolean2Objects", true), CommandStatus::Partial, "Produces the symmetric difference; cycling through results is planned.");
  Reg(e, "MeshBooleanUnion", Make<BooleanCommand>(kernel::BooleanOp::Union, "MeshBooleanUnion", false));
  Reg(e, "MeshBooleanDifference", Make<BooleanCommand>(kernel::BooleanOp::Difference, "MeshBooleanDifference", true));
  Reg(e, "MeshBooleanIntersection", Make<BooleanCommand>(kernel::BooleanOp::Intersection, "MeshBooleanIntersection", true));
  // "Split" itself lives in cmd_curveedit.cpp (curves by curves); it delegates solids to BooleanSplit.
  Reg(e, "BooleanSplit", Make<SplitPlaneCommand>(), CommandStatus::Partial, "Plane split.");
  Reg(e, "MeshSplit", Make<SplitPlaneCommand>(), CommandStatus::Partial, "Plane split.");
  Reg(e, "MeshBooleanSplit", Make<SplitPlaneCommand>(), CommandStatus::Partial, "Plane split.");
  Reg(e, "WireCut", Make<SplitPlaneCommand>(), CommandStatus::Partial, "Plane cut through two points.");
  Reg(e, "ReduceMesh", OnSelection("Select meshes to reduce", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("ReduceMesh");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { try { *o->mesh = kernel::Simplify(*o->mesh, ctx.Settings().absolute_tolerance * 100); o->InvalidateDisplay(); } catch (const std::exception& ex) { ctx.Warn(ex.what()); } } }
      }), CommandStatus::Partial, "Simplifies with a fixed tolerance; target count is planned.");
  Reg(e, "MeshSmooth", OnSelection("Select meshes to smooth", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("MeshSmooth");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { try { kernel::BoundingBox bb = o->mesh->GetBoundingBox(); double len = (bb.max - bb.min).Length() / 30; *o->mesh = kernel::SmoothAndRefine(*o->mesh, len); o->InvalidateDisplay(); } catch (const std::exception& ex) { ctx.Warn(ex.what()); } } }
      }), CommandStatus::Partial);
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
