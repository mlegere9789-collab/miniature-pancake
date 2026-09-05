// Extended selection commands: Select by name/id, duplicates, fence /
// circular / volume picks, annotation and block selection, user-text
// matching, sub-object selection filters, group naming.
#include "commands/cmd_common.h"

#include <cctype>
#include <map>
#include <set>

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

// Polyline sampling of a curve for the self-intersection test.
std::vector<Point3d> Sample(const kernel::NurbsCurve& c, int n) {
  std::vector<Point3d> pts;
  const kernel::Interval d = c.Domain();
  for (int i = 0; i <= n; ++i) pts.push_back(c.PointAt(d.min + (d.max - d.min) * i / n));
  return pts;
}

bool SelfIntersects(const kernel::NurbsCurve& c, double tol) {
  const int n = std::max(32, c.ControlPointCount() * 8);
  const std::vector<Point3d> pts = Sample(c, n);
  const bool closed = c.IsClosed();
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    for (size_t j = i + 2; j + 1 < pts.size(); ++j) {
      if (closed && i == 0 && j + 2 == pts.size()) continue;  // the closing seam
      ON_Line a(pts[i], pts[i + 1]), b(pts[j], pts[j + 1]);
      double ta = 0, tb = 0;
      if (!ON_IntersectLineLine(a, b, &ta, &tb, tol, true)) continue;
      if (a.PointAt(ta).DistanceTo(b.PointAt(tb)) <= tol) return true;
    }
  }
  return false;
}

// Non-manifold edge test on a mesh: any edge shared by more than two faces.
bool HasNonManifoldEdge(const kernel::Mesh& m) {
  std::map<std::pair<int, int>, int> edges;
  const ON_Mesh& raw = m.raw();
  for (int fi = 0; fi < raw.m_F.Count(); ++fi) {
    const ON_MeshFace& f = raw.m_F[fi];
    const int n = f.IsQuad() ? 4 : 3;
    for (int k = 0; k < n; ++k) {
      int a = f.vi[k], b = f.vi[(k + 1) % n];
      if (a == b) continue;
      if (a > b) std::swap(a, b);
      if (++edges[{a, b}] > 2) return true;
    }
  }
  return false;
}

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
  Reg(e, "SelBrush", Make<SelCircularCommand>(), CommandStatus::Partial, "Selects inside one circle; painting a brush stroke is planned.");
  Reg(e, "SelBrushPoints", Make<SelCircularCommand>(), CommandStatus::Partial, "Selects whole objects inside one circle.");

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
        for (SceneObject& o : ctx.Doc().Objects()) if (o.kind == ObjectKind::Curve && Selectable(ctx, o) && SelfIntersects(*o.curve, tol)) { o.selected = true; ++n; }
        ctx.Print("SelSelfIntersectingCrv: " + std::to_string(n) + " curve(s) selected");
      }), CommandStatus::Partial, "Tests a polyline sampling of each curve.");
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

  for (const char* n : {"SelControlPoint", "SelControls", "SelControlPointRegion", "SelU", "SelV", "SelUV", "SelEdgeLoop", "SelEdgeRing", "SelFaceLoop",
                        "SelFacesToBoundary", "SelMeshEdges", "SelSubDEdges", "SelMeshPart", "SelNakedMeshEdgePt"})
    Reg(e, n, SubObjectPlanned(n), CommandStatus::Partial, sub);
  Reg(e, "SelNonManifold", SelWhere([](CommandContext&, const SceneObject& o) { return o.kind == ObjectKind::Mesh && HasNonManifoldEdge(*o.mesh); }), CommandStatus::Partial, "Selects whole meshes that have non-manifold edges.");

  // Blocks.
  Reg(e, "SelBlockInstanceNamed", Make<TextArgCommand>("Block name", [](CommandContext& ctx, const std::string& name) {
        ctx.Doc().SelectWhere([&](const SceneObject& o) { auto it = o.user_text.find("Block"); return Selectable(ctx, o) && it != o.user_text.end() && Lower(it->second) == Lower(name); }, true);
        ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) of block '" + name + "' selected");
      }));
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
  Reg(e, "SelLinetype", Make<TextArgCommand>("Linetype name", [](CommandContext& ctx, const std::string& name) {
        ctx.Doc().SelectWhere([&](const SceneObject& o) {
          if (!Selectable(ctx, o)) return false;
          auto it = o.user_text.find("Linetype");
          if (it != o.user_text.end()) return Lower(it->second) == Lower(name);
          return o.layer_index >= 0 && o.layer_index < static_cast<int>(ctx.Doc().Layers().size()) && Lower(ctx.Doc().Layers()[static_cast<size_t>(o.layer_index)].linetype) == Lower(name);
        }, true);
        Report(ctx);
      }));
  Reg(e, "SelFontUse", SelGroupNamed({"Text", "TextObject", "Leader", "DimLinear", "DimAligned", "DimAngle", "DimRadius", "DimDiameter"}), CommandStatus::Partial, "Selects all annotation (one font is used).");
  Reg(e, "SelAnnotationStyle", SelGroupNamed({"Text", "TextObject", "Leader", "DimLinear", "DimAligned", "DimAngle", "DimRadius", "DimDiameter"}), CommandStatus::Partial, "Selects all annotation (one style is used).");
  Reg(e, "SelDimOverride", NoSuchObjects("dimensions with style overrides"), CommandStatus::Partial);
  Reg(e, "SelDimTextOverride", NoSuchObjects("dimensions with text overrides"), CommandStatus::Partial);
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
  Reg(e, "HidePt", Immediate([](CommandContext& ctx) { int n = 0; for (SceneObject& o : ctx.Doc().Objects()) if (o.selected && o.show_control_points) { o.show_control_points = false; ++n; } ctx.Print("HidePt: control points hidden on " + std::to_string(n) + " object(s)"); }), CommandStatus::Partial, "Hides the control points of the selected objects.");
  Reg(e, "ShowPt", Immediate([](CommandContext& ctx) { int n = 0; for (SceneObject& o : ctx.Doc().Objects()) if (o.selected) { o.show_control_points = true; ++n; } ctx.Print("ShowPt: control points shown on " + std::to_string(n) + " object(s)"); }), CommandStatus::Partial, "Shows every control point of the selected objects.");
  Reg(e, "InvertPt", Immediate([](CommandContext& ctx) { ctx.Print(std::string("InvertPt: ") + "control-point selection is coming; Invert flips the object selection."); }), CommandStatus::Partial, sub);
}

}  // namespace dino8::app
