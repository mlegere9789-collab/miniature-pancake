// Analysis commands: Distance, Length, Area, Volume, What, List, BoundingBox...
#include "commands/cmd_common.h"

#include <sstream>

#include "gl/gl_loader.h"

namespace dino8::app {

namespace {

double ObjectArea(const SceneObject& o) {
  switch (o.kind) {
    case ObjectKind::Surface: return o.surface ? o.surface->ApproximateArea() : 0;
    case ObjectKind::Mesh: return o.mesh ? o.mesh->Area() : 0;
    case ObjectKind::Brep: { std::optional<kernel::Mesh> m = MeshOf(o, 0.005); return m ? m->Area() : 0; }
    case ObjectKind::SubD: return o.subd ? o.subd->ToApproximateMesh().Area() : 0;
    default: return 0;
  }
}

double ObjectVolume(const SceneObject& o, bool& closed) {
  std::optional<kernel::Mesh> m = MeshOf(o, 0.005);
  closed = m && m->IsClosedManifold();
  return closed ? std::fabs(m->Volume()) : 0;
}

// ---------------------------------------------------------------------------
// Surface analysis display modes (Zebra / EMap / CurvatureAnalysis /
// DraftAngleAnalysis). Like Rhino, the mode is stored per object; running a
// command with nothing selected applies it to every surface through the
// app-wide fallback. Options come from the command line, e.g.
// "Zebra Direction=Vertical Density=12", so scripts and macros work.
// ---------------------------------------------------------------------------

bool SurfaceLike(const SceneObject& o) {
  return o.kind == ObjectKind::Surface || o.kind == ObjectKind::Brep || o.kind == ObjectKind::Mesh || o.kind == ObjectKind::SubD;
}

bool ParseDouble(const std::string& t, double& out) {
  char* end = nullptr;
  out = std::strtod(t.c_str(), &end);
  return end && *end == 0 && !t.empty();
}

// Consumes "Name=Value" tokens left on the command line into `s`. Unknown
// options are reported and ignored.
void ParseAnalysisOptions(CommandContext& ctx, AnalysisSettings& s) {
  while (auto tok = ctx.Engine().TakePendingInput()) {
    const std::string& t = *tok;
    const size_t eq = t.find('=');
    const std::string name = ToLower(eq == std::string::npos ? t : t.substr(0, eq));
    const std::string value = eq == std::string::npos ? "" : t.substr(eq + 1);
    const std::string lv = ToLower(value);
    double d = 0;
    if (name == "direction" && s.mode == AnalysisMode::Zebra) {
      if (lv == "vertical" || lv == "v") s.zebra_direction = ZebraDirection::Vertical;
      else if (lv == "horizontal" || lv == "h") s.zebra_direction = ZebraDirection::Horizontal;
      else s.zebra_direction = s.zebra_direction == ZebraDirection::Horizontal ? ZebraDirection::Vertical : ZebraDirection::Horizontal;
    } else if (name == "density" && ParseDouble(value, d)) {
      s.zebra_density = static_cast<float>(std::clamp(d, 1.0, 64.0));
    } else if (name == "style") {
      if (lv == "mean" || lv == "m") s.curvature_style = CurvatureStyle::Mean;
      else if (lv == "gaussian" || lv == "g") s.curvature_style = CurvatureStyle::Gaussian;
      else s.curvature_style = s.curvature_style == CurvatureStyle::Gaussian ? CurvatureStyle::Mean : CurvatureStyle::Gaussian;
    } else if (name == "autorange") {
      s.auto_range = lv.empty() ? !s.auto_range : (lv == "yes" || lv == "y" || lv == "true" || lv == "1");
    } else if ((name == "min" || name == "minangle") && ParseDouble(value, d)) {
      if (s.mode == AnalysisMode::DraftAngle) s.draft_min = d; else { s.range_min = d; s.auto_range = false; }
    } else if ((name == "max" || name == "maxangle") && ParseDouble(value, d)) {
      if (s.mode == AnalysisMode::DraftAngle) s.draft_max = d; else { s.range_max = d; s.auto_range = false; }
    } else if (name == "direction" || name == "pulldirection") {
      if (lv == "cplane" || lv == "c") s.draft_direction = ActiveNormal(ctx);
      else if (lv == "world" || lv == "w" || lv == "z") s.draft_direction = Vector3d(0, 0, 1);
      else if (lv == "-z" || lv == "-world") s.draft_direction = Vector3d(0, 0, -1);
      else {
        double x = 0, y = 0, z = 0;
        if (std::sscanf(value.c_str(), "%lf,%lf,%lf", &x, &y, &z) == 3) s.draft_direction = Vector3d(x, y, z);
        else ctx.Warn("Direction: expected CPlane, World or x,y,z");
      }
    } else {
      ctx.Warn("Unknown option: " + t);
    }
  }
  if (s.range_max < s.range_min) std::swap(s.range_min, s.range_max);
  if (s.draft_max < s.draft_min) std::swap(s.draft_min, s.draft_max);
}

std::string DescribeAnalysis(const AnalysisSettings& s) {
  switch (s.mode) {
    case AnalysisMode::Zebra:
      return std::string("Direction=") + (s.zebra_direction == ZebraDirection::Vertical ? "Vertical" : "Horizontal") + " Density=" + FormatNumber(s.zebra_density);
    case AnalysisMode::Curvature:
      return std::string("Style=") + (s.curvature_style == CurvatureStyle::Mean ? "Mean" : "Gaussian") + " AutoRange=" + (s.auto_range ? "Yes" : "No") +
             (s.auto_range ? "" : " Min=" + FormatNumber(s.range_min) + " Max=" + FormatNumber(s.range_max));
    case AnalysisMode::DraftAngle:
      return "Direction=" + FormatPoint(Point3d(s.draft_direction.x, s.draft_direction.y, s.draft_direction.z)) + " MinAngle=" + FormatNumber(s.draft_min) + " MaxAngle=" + FormatNumber(s.draft_max);
    default: return "";
  }
}

// Turns `mode` on for the selected surfaces, or for every surface (via the
// app-wide fallback) when nothing is selected.
CommandFactory AnalysisOn(AnalysisMode mode) {
  return Immediate([mode](CommandContext& ctx) {
    AnalysisSettings s = ctx.App().analysis_defaults;
    s.mode = mode;
    ParseAnalysisOptions(ctx, s);
    ctx.App().analysis_defaults = s;
    std::vector<SceneObject*> targets;
    for (ObjectId id : ctx.Selected()) if (SceneObject* o = ctx.Doc().Find(id)) if (SurfaceLike(*o)) targets.push_back(o);
    size_t count = targets.size();
    if (targets.empty()) {
      ctx.App().analysis_fallback = s;
      count = 0;
      for (SceneObject& o : ctx.Doc().Objects()) {
        o.analysis.mode = AnalysisMode::None;  // the fallback now applies everywhere
        if (SurfaceLike(o)) { ++count; targets.push_back(&o); }
      }
    } else {
      for (SceneObject* o : targets) o->analysis = s;
    }
    std::string msg = std::string(AnalysisModeName(mode)) + " on " + std::to_string(count) + " object(s)";
    const std::string opts = DescribeAnalysis(s);
    if (!opts.empty()) msg += "  " + opts;
    if (mode == AnalysisMode::Curvature && !targets.empty()) {
      const SceneObject& o = *targets.front();
      o.EnsureDisplay(ctx.App().curve_display_tolerance, ctx.App().surface_display_tolerance);
      o.EnsureAnalysisColors(s);
      msg += "  range " + FormatNumber(o.Display().colors_min) + " (blue) .. " + FormatNumber(o.Display().colors_max) + " (red)";
    }
    if (count == 0) ctx.Warn("No surfaces, polysurfaces or meshes to analyze");
    ctx.Print(msg);
    ctx.RequestRedraw();
  });
}

CommandFactory AnalysisOff(AnalysisMode mode) {
  return Immediate([mode](CommandContext& ctx) {
    size_t count = 0;
    if (ctx.App().analysis_fallback.mode == mode) { ctx.App().analysis_fallback.mode = AnalysisMode::None; ++count; }
    for (SceneObject& o : ctx.Doc().Objects()) if (o.analysis.mode == mode) { o.analysis.mode = AnalysisMode::None; ++count; }
    ctx.Print(std::string(AnalysisModeName(mode)) + " off" + (count ? "" : " (was not on)"));
    ctx.RequestRedraw();
  });
}

}  // namespace

void RegisterAnalyzeCommands(CommandEngine& e) {
  Reg(e, "Distance", Make<PointsCommand>(std::vector<std::string>{"First point for distance", "Second point for distance"},
                                         [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                           Vector3d d = p[1] - p[0];
                                           ctx.Print("Distance = " + FormatNumber(d.Length()) + " " + ctx.Settings().unit_system + "  (dx " + FormatNumber(d.x) + ", dy " + FormatNumber(d.y) + ", dz " + FormatNumber(d.z) + ")");
                                           ctx.App().Notify("Distance " + FormatNumber(d.Length()));
                                         },
                                         [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) { ctx.AddPreviewLine(p[0], h); }));
  Reg(e, "Angle", Make<PointsCommand>(std::vector<std::string>{"Vertex of angle", "First direction point", "Second direction point"},
                                      [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                        Vector3d a = p[1] - p[0], b = p[2] - p[0];
                                        double ang = ON_3dVector::Angle(a, b) * 180.0 / ON_PI;
                                        ctx.Print("Angle = " + FormatNumber(ang) + " degrees");
                                      },
                                      [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) { ctx.AddPreviewLine(p[0], h); if (p.size() > 1) ctx.AddPreviewLine(p[0], p[1]); }));
  Reg(e, "Length", OnSelection("Select curves to measure", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        double total = 0;
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) { double l = o->curve->Length(); total += l; ctx.Print("Curve " + std::to_string(id) + " length = " + FormatNumber(l)); } }
        ctx.Print("Total length = " + FormatNumber(total) + " " + ctx.Settings().unit_system);
      }));
  Reg(e, "Area", OnSelection("Select surfaces, polysurfaces or meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        double total = 0;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) total += ObjectArea(*o);
        ctx.Print("Area = " + FormatNumber(total) + " square " + ctx.Settings().unit_system);
      }));
  Reg(e, "Volume", OnSelection("Select closed solids", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        double total = 0;
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o) continue; bool closed; double v = ObjectVolume(*o, closed); if (!closed) ctx.Warn("Object " + std::to_string(id) + " is not closed"); total += v; }
        ctx.Print("Volume = " + FormatNumber(total) + " cubic " + ctx.Settings().unit_system);
      }));
  Reg(e, "VolumeCentroid", OnSelection("Select closed solids", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o) continue; std::optional<kernel::Mesh> m = MeshOf(*o, 0.005); if (!m) continue; Point3d c = m->GetCentroid(); ctx.Print("Centroid " + FormatPoint(c)); AddObject(ctx, SceneObject::MakePoint(c), "VolumeCentroid"); }
      }));
  Reg(e, "AreaCentroid", OnSelection("Select surfaces or meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o) continue; std::optional<kernel::Mesh> m = MeshOf(*o, 0.01); if (!m) continue; kernel::BoundingBox bb = m->GetBoundingBox(); Point3d c = (bb.min + bb.max) / 2.0; ctx.Print("Area centroid (bounding-box estimate) " + FormatPoint(c)); AddObject(ctx, SceneObject::MakePoint(c), "AreaCentroid"); }
      }), CommandStatus::Partial, "Bounding-box centroid estimate.");
  Reg(e, "BoundingBox", OnSelection("Select objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        kernel::BoundingBox bb;
        if (!ctx.Doc().BoundingBoxOf(ids, bb)) return;
        Point3d& a = bb.min; Point3d& b = bb.max;
        ctx.Print("Bounding box min " + FormatPoint(a) + " max " + FormatPoint(b));
        std::vector<Point3d> bottom = {a, Point3d(b.x, a.y, a.z), Point3d(b.x, b.y, a.z), Point3d(a.x, b.y, a.z), a};
        std::vector<Point3d> top = {Point3d(a.x, a.y, b.z), Point3d(b.x, a.y, b.z), b, Point3d(a.x, b.y, b.z), Point3d(a.x, a.y, b.z)};
        ctx.Doc().BeginChange("BoundingBox");
        if (std::fabs(b.z - a.z) < 1e-9) { ctx.Doc().Add(SceneObject::MakeCurve(PolylineCurve(bottom))); return; }
        ON_3dPoint corners[8] = {bottom[0], bottom[1], bottom[2], bottom[3], top[0], top[1], top[2], top[3]};
        if (ON_Brep* br = ON_BrepBox(corners)) { kernel::Brep k; k.raw() = *br; delete br; ctx.Doc().Add(SceneObject::MakeBrep(k)); }
      }));
  Reg(e, "What", OnSelection("Select objects to describe", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) { std::istringstream in(o->Describe()); std::string line; while (std::getline(in, line)) ctx.Print(line); }
        ctx.App().Panels().command_history = true;
      }));
  Reg(e, "List", OnSelection("Select objects to list", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          ctx.Print("Object " + std::to_string(id) + " (" + ObjectKindName(o->kind) + ") layer " + ctx.Doc().LayerFullPath(o->layer_index) + (o->name.empty() ? "" : " name '" + o->name + "'"));
          if (o->kind == ObjectKind::Curve) { const ON_NurbsCurve& c = o->curve->raw(); ctx.Print("  degree " + std::to_string(c.Degree()) + ", " + std::to_string(c.CVCount()) + " control points, " + (c.IsRational() ? "rational" : "non-rational") + ", " + (c.IsClosed() ? "closed" : "open")); for (int i = 0; i < c.CVCount(); ++i) { ON_3dPoint p; c.GetCV(i, p); ctx.Print("  CV[" + std::to_string(i) + "] " + FormatPoint(p)); } }
          else if (o->kind == ObjectKind::Surface) { ctx.Print("  degree " + std::to_string(o->surface->DegreeU()) + " x " + std::to_string(o->surface->DegreeV()) + ", CVs " + std::to_string(o->surface->CVCountU()) + " x " + std::to_string(o->surface->CVCountV())); }
          else if (o->kind == ObjectKind::Brep) { ctx.Print("  " + std::to_string(o->brep->FaceCount()) + " faces, " + std::to_string(o->brep->raw().m_E.Count()) + " edges, " + (o->brep->raw().IsSolid() ? "closed solid" : "open")); }
          else if (o->kind == ObjectKind::Mesh) { ctx.Print("  " + std::to_string(o->mesh->VertexCount()) + " vertices, " + std::to_string(o->mesh->FaceCount()) + " faces"); }
          else if (o->kind == ObjectKind::SubD) { ctx.Print("  " + std::to_string(o->subd->FaceCount()) + " faces, " + std::to_string(o->subd->EdgeCount()) + " edges, " + std::to_string(o->subd->VertexCount()) + " vertices, " + std::to_string(o->subd->CreaseEdgeCount()) + " creases"); }
          else if (o->kind == ObjectKind::Point) ctx.Print("  " + FormatPoint(o->point));
        }
        ctx.App().Panels().command_history = true;
      }));
  Reg(e, "Check", OnSelection("Select objects to check", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          bool ok = true;
          if (o->kind == ObjectKind::Curve) ok = o->curve->raw().IsValid();
          else if (o->kind == ObjectKind::Surface) ok = o->surface->raw().IsValid();
          else if (o->kind == ObjectKind::Brep) ok = o->brep->raw().IsValid();
          else if (o->kind == ObjectKind::Mesh) ok = o->mesh->raw().IsValid();
          else if (o->kind == ObjectKind::SubD) ok = o->subd->raw().IsValid();
          ctx.Print("Object " + std::to_string(id) + ": " + (ok ? "valid" : "INVALID"));
        }
      }));
  Reg(e, "SelBadObjects", Immediate([](CommandContext& ctx) {
        ctx.Doc().SelectWhere([](const SceneObject& o) {
          if (o.kind == ObjectKind::Curve) return !o.curve->raw().IsValid();
          if (o.kind == ObjectKind::Surface) return !o.surface->raw().IsValid();
          if (o.kind == ObjectKind::Brep) return !o.brep->raw().IsValid();
          if (o.kind == ObjectKind::Mesh) return !o.mesh->raw().IsValid();
          return false;
        });
        ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " bad object(s) selected");
      }));
  Reg(e, "EvaluatePt", Make<PointsCommand>(std::vector<std::string>{"Point to evaluate"}, [](CommandContext& ctx, const std::vector<Point3d>& p) { ctx.Print("Point " + FormatPoint(p[0])); ctx.App().Notify(FormatPoint(p[0])); }));
  Reg(e, "Radius", OnSelection("Select a curve", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o || o->kind != ObjectKind::Curve) continue; kernel::Interval d = o->curve->Domain(); Vector3d k = o->curve->CurvatureAt((d.min + d.max) / 2); double kk = k.Length(); ctx.Print(kk > 1e-12 ? "Radius at curve midpoint = " + FormatNumber(1.0 / kk) : "Curve is straight at its midpoint"); }
      }), CommandStatus::Partial, "Reports the radius at the curve midpoint; picking a point is planned.");
  Reg(e, "Diameter", OnSelection("Select a curve", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o || o->kind != ObjectKind::Curve) continue; kernel::Interval d = o->curve->Domain(); double kk = o->curve->CurvatureAt((d.min + d.max) / 2).Length(); ctx.Print(kk > 1e-12 ? "Diameter at curve midpoint = " + FormatNumber(2.0 / kk) : "Curve is straight at its midpoint"); }
      }), CommandStatus::Partial);
  Reg(e, "Curvature", OnSelection("Select a curve", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o || o->kind != ObjectKind::Curve) continue; kernel::Interval d = o->curve->Domain(); for (int i = 0; i <= 10; ++i) { double t = d.min + (d.max - d.min) * i / 10.0; ctx.Print("t=" + FormatNumber(t) + " curvature " + FormatNumber(o->curve->CurvatureAt(t).Length())); } }
      }), CommandStatus::Partial, "Prints curvature at 11 samples; the on-screen graph is planned.");
  Reg(e, "CurvatureGraph", OnSelection("Select curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("CurvatureGraph");
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o || o->kind != ObjectKind::Curve) continue; kernel::Interval d = o->curve->Domain(); std::vector<Point3d> pts; for (int i = 0; i <= 60; ++i) { double t = d.min + (d.max - d.min) * i / 60.0; Vector3d k = o->curve->CurvatureAt(t); pts.push_back(o->curve->PointAt(t) - k * 20.0); } SceneObject g = SceneObject::MakeCurve(PolylineCurve(pts)); g.name = "CurvatureGraph"; g.color = Color::FromBytes(255, 120, 40); g.color_by_layer = false; ctx.Doc().Add(std::move(g)); }
      }), CommandStatus::Partial, "Draws the graph as a curve object; delete it when done.");
  Reg(e, "Zebra", AnalysisOn(AnalysisMode::Zebra));
  Reg(e, "ZebraOff", AnalysisOff(AnalysisMode::Zebra));
  Reg(e, "EMap", AnalysisOn(AnalysisMode::EMap));
  Reg(e, "EMapOff", AnalysisOff(AnalysisMode::EMap));
  Reg(e, "CurvatureAnalysis", AnalysisOn(AnalysisMode::Curvature));
  Reg(e, "CurvatureAnalysisOff", AnalysisOff(AnalysisMode::Curvature));
  Reg(e, "DraftAngleAnalysis", AnalysisOn(AnalysisMode::DraftAngle));
  Reg(e, "DraftAngleAnalysisOff", AnalysisOff(AnalysisMode::DraftAngle));
  Reg(e, "ShowEdges", OnSelection("Select surfaces, polysurfaces or meshes to show edges", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        size_t objects = 0, edges = 0, naked = 0;
        for (ObjectId id : ids) {
          SceneObject* o = ctx.Doc().Find(id);
          if (!o || !SurfaceLike(*o)) continue;
          o->highlight_edges = true;
          ++objects;
          if (o->kind == ObjectKind::Brep) {
            const ON_Brep& b = o->brep->raw();
            for (int i = 0; i < b.m_E.Count(); ++i) { if (b.m_E[i].m_edge_index < 0) continue; ++edges; if (b.m_E[i].TrimCount() == 1) ++naked; }
          } else if (o->kind == ObjectKind::Surface) { edges += 4; naked += 4; }
          else {
            o->EnsureDisplay(ctx.App().curve_display_tolerance, ctx.App().surface_display_tolerance);
            edges += o->Display().edges.size() / 6; naked += o->Display().naked_edges.size() / 6;
          }
        }
        ctx.Print("ShowEdges: " + std::to_string(objects) + " object(s), " + std::to_string(edges) + " edge(s), " + std::to_string(naked) + " naked edge(s) (magenta = edges, red = naked edges)");
        ctx.RequestRedraw();
      }));
  Reg(e, "ShowEdgesOff", Immediate([](CommandContext& ctx) {
        size_t n = 0;
        for (SceneObject& o : ctx.Doc().Objects()) if (o.highlight_edges) { o.highlight_edges = false; ++n; }
        ctx.Print("ShowEdges off (" + std::to_string(n) + " object(s))");
        ctx.RequestRedraw();
      }));
  Reg(e, "CrvDeviation", OnSelection("Select two curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        if (ids.size() < 2) return;
        const SceneObject* a = ctx.Doc().Find(ids[0]); const SceneObject* b = ctx.Doc().Find(ids[1]);
        if (!a || !b || a->kind != ObjectKind::Curve || b->kind != ObjectKind::Curve) return;
        double mn = 1e300, mx = 0;
        for (double t : a->curve->DivideByCount(100)) { double d = (b->curve->ClosestPoint(a->curve->PointAt(t)) - a->curve->PointAt(t)).Length(); mn = std::min(mn, d); mx = std::max(mx, d); }
        ctx.Print("Deviation: min " + FormatNumber(mn) + " max " + FormatNumber(mx));
      }, 2));
  Reg(e, "PointDeviation", OnSelection("Select points then a surface or mesh", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        const SceneObject* target = nullptr;
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind != ObjectKind::Point) target = o; }
        if (!target) { ctx.Warn("Select a surface or mesh too"); return; }
        std::optional<kernel::Mesh> m = MeshOf(*target, 0.005);
        if (!m) return;
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Point) ctx.Print("Point " + FormatPoint(o->point) + " deviation " + FormatNumber((m->ClosestPoint(o->point) - o->point).Length())); }
      }, 2));
  // "Intersect" lives in cmd_curveedit.cpp (curve/curve, with the solid/solid volume as fallback).
  Reg(e, "Audit", Immediate([](CommandContext& ctx) {
        int bad = 0;
        for (const SceneObject& o : ctx.Doc().Objects()) { bool ok = true; if (o.kind == ObjectKind::Curve) ok = o.curve->raw().IsValid(); else if (o.kind == ObjectKind::Surface) ok = o.surface->raw().IsValid(); else if (o.kind == ObjectKind::Brep) ok = o.brep->raw().IsValid(); else if (o.kind == ObjectKind::Mesh) ok = o.mesh->raw().IsValid(); if (!ok) ++bad; }
        ctx.Print("Audit: " + std::to_string(ctx.Doc().ObjectCount()) + " objects, " + std::to_string(bad) + " invalid, " + std::to_string(ctx.Doc().Layers().size()) + " layers, " + std::to_string(ctx.Doc().Groups().size()) + " groups");
      }));
  Reg(e, "SystemInfo", Immediate([](CommandContext& ctx) {
        ctx.Print(std::string("Dino 8 ") + DINO8_VERSION + " - free, no subscription");
        ctx.Print(std::string("OpenGL: ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)) + " / " + reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        ctx.Print("OpenNURBS " + std::to_string(ON::Version()));
        ctx.App().Panels().command_history = true;
      }));
}

}  // namespace dino8::app
