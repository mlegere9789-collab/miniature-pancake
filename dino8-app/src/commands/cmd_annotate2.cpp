// More annotation, hatch, linetype and block tools: DimArea / DimCurveLength /
// DimVolume / DimOrdinate / DimCreaseAngle, Centermark, Arrowhead, RevCloud,
// text editing (TextProperties, MatchAnnotation, ScaleTextHeight, FindText),
// annotation styles, Dot / ConvertDots, HatchBase / HatchScale, the linetype
// commands, block editing extras, and Picture.
//
// Every option is also accepted as a "Name=Value" token after the command
// name so scripts can drive them (see tests/annotate2_script.txt).
#include "commands/annotate_common.h"
#include "commands/cmd_common.h"
#include "commands/hatch_common.h"
#include "io/File3dm.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace dino8::app {

namespace {

std::string Units(CommandContext& ctx) { return ctx.Settings().unit_system; }

std::string LowerStr(const std::string& s) { return ToLower(s); }

bool YesLike(const std::string& v) { const std::string l = LowerStr(v); return l == "yes" || l == "y" || l == "1" || l == "true" || l == "on"; }

// Arrow triangle at `tip` pointing along `dir`.
kernel::NurbsCurve ArrowCurve(Point3d tip, Vector3d dir, double size, const ON_Plane& pl) {
  dir.Unitize();
  Vector3d side = ON_CrossProduct(pl.zaxis, dir);
  side.Unitize();
  const Point3d a = tip - dir * size + side * (size * 0.3), b = tip - dir * size - side * (size * 0.3);
  return PolylineCurve({a, tip, b, a});
}

// Area enclosed by a closed planar curve (vector area of its sampled polygon).
double ClosedCurveArea(const kernel::NurbsCurve& c) {
  const std::vector<Point3d> poly = BoundaryPolygon(c);
  if (poly.size() < 3) return 0;
  Vector3d sum(0, 0, 0);
  for (size_t i = 0; i < poly.size(); ++i) {
    const Point3d& p = poly[i];
    const Point3d& q = poly[(i + 1) % poly.size()];
    sum += ON_CrossProduct(Vector3d(p.x, p.y, p.z), Vector3d(q.x, q.y, q.z));
  }
  return sum.Length() / 2;
}

double AreaOf(const SceneObject& o) {
  switch (o.kind) {
    case ObjectKind::Curve: return o.curve && o.curve->IsClosed() ? ClosedCurveArea(*o.curve) : 0;
    case ObjectKind::Surface: return o.surface ? o.surface->ApproximateArea() : 0;
    case ObjectKind::Mesh: return o.mesh ? o.mesh->Area() : 0;
    case ObjectKind::Brep: { std::optional<kernel::Mesh> m = MeshOf(o, 0.005); return m ? m->Area() : 0; }
    case ObjectKind::SubD: return o.subd ? o.subd->ToApproximateMesh().Area() : 0;
    default: return 0;
  }
}

Point3d CenterOf(const SceneObject& o) {
  const kernel::BoundingBox b = o.BoundingBox();
  return (b.min + b.max) / 2.0;
}

// A leader annotation: arrow at `anchor`, landing at `at`, text after it.
int AddLeaderText(CommandContext& ctx, const std::string& kind, Point3d anchor, Point3d at, const std::string& text) {
  const ON_Plane pl = ActivePlane(ctx);
  const double h = AnnotationTextHeight(ctx);
  std::vector<kernel::NurbsCurve> curves;
  if (anchor.DistanceTo(at) > 1e-9) {
    curves.push_back(PolylineCurve({anchor, at}));
    curves.push_back(ArrowCurve(anchor, anchor - at, AnnotationArrowSize(ctx), pl));
  }
  GlyphSpec g;
  g.text = text; g.height = h; g.plane = pl; g.center = false;
  g.plane.SetOrigin(at + pl.xaxis * (h * 0.4) - pl.yaxis * (h * 0.5));
  return AddAnnotationGroup(ctx, kind, curves, g);
}

// ---------------------------------------------------------------------------
// Measured dimensions: DimArea, DimCurveLength, DimVolume
// ---------------------------------------------------------------------------

class MeasureDimCommand : public Command {
 public:
  enum class Kind { Area, Length, Volume };
  explicit MeasureDimCommand(Kind k) : kind_(k) {}
  void Begin(CommandContext&) override {
    switch (kind_) {
      case Kind::Area: WantObjects("Select a closed planar curve, surface or polysurface"); break;
      case Kind::Length: WantObjects("Select a curve"); break;
      case Kind::Volume: WantObjects("Select a closed surface, polysurface or mesh"); break;
    }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      double v = 0;
      if (kind_ == Kind::Area) v = AreaOf(*o);
      else if (kind_ == Kind::Length) v = o->kind == ObjectKind::Curve && o->curve ? o->curve->Length() : 0;
      else { std::optional<kernel::Mesh> m = MeshOf(*o, 0.005); if (m && m->IsClosedManifold()) v = std::fabs(m->Volume()); }
      if (v <= 0) continue;
      value_ += v;
      anchor_ = CenterOf(*o);
      if (kind_ == Kind::Length && o->kind == ObjectKind::Curve) anchor_ = o->curve->PointAt(o->curve->Domain().min + (o->curve->Domain().max - o->curve->Domain().min) / 2);
      ++count_;
    }
    if (count_ == 0) { ctx.Warn(kind_ == Kind::Area ? "Select a closed planar curve, surface or polysurface" : kind_ == Kind::Length ? "Select a curve" : "Select a closed object"); Finish(); return; }
    WantPoint("Leader location");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.ClearPreview();
    std::string label, text;
    switch (kind_) {
      case Kind::Area: label = "DimArea"; text = "Area = " + FormatNumber(value_) + " square " + Units(ctx); break;
      case Kind::Length: label = "DimCurveLength"; text = "Length = " + FormatNumber(value_) + " " + Units(ctx); break;
      case Kind::Volume: label = "DimVolume"; text = "Volume = " + FormatNumber(value_) + " cubic " + Units(ctx); break;
    }
    ctx.Doc().BeginChange(label);
    AddLeaderText(ctx, label, anchor_, p, text);
    ctx.Print(label + ": " + text);
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (count_ > 0) { ctx.ClearPreview(); ctx.AddPreviewLine(anchor_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Kind kind_;
  double value_ = 0;
  int count_ = 0;
  Point3d anchor_;
};

// ---------------------------------------------------------------------------
// DimOrdinate: base point, then feature points -> X or Y ordinate leaders.
// ---------------------------------------------------------------------------

class DimOrdinateCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    const std::string d = OptionOr(TakeOptionTokens(ctx), "direction");
    if (!d.empty()) dir_ = std::toupper(static_cast<unsigned char>(d[0])) == 'Y' ? 'Y' : 'X';
    options = {{"Direction", std::string(1, dir_), {"X", "Y"}, false, false}};
    WantPoint("Ordinate base point");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Direction") { dir_ = (v.empty() ? (dir_ == 'X' ? 'Y' : 'X') : (std::toupper(static_cast<unsigned char>(v[0])) == 'Y' ? 'Y' : 'X')); options[0].value = std::string(1, dir_); }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!base_) { base_ = p; WantPoint("Feature point (Enter to finish)"); return; }
    const ON_Plane pl = ActivePlane(ctx);
    const double h = AnnotationTextHeight(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(*base_, &u0, &v0);
    pl.ClosestPointTo(p, &u1, &v1);
    const double value = dir_ == 'X' ? u1 - u0 : v1 - v0;
    // The leader runs away from the feature along the other axis.
    const Vector3d leader = dir_ == 'X' ? pl.yaxis : pl.xaxis;
    const Point3d end = p + leader * (h * 2.5);
    std::vector<kernel::NurbsCurve> curves = {PolylineCurve({p, end})};
    GlyphSpec g;
    g.text = std::string(1, dir_) + " " + FormatNumber(value);
    g.height = h; g.plane = pl; g.center = dir_ == 'X';
    g.plane.SetOrigin(dir_ == 'X' ? end + pl.yaxis * (h * 0.3) : end + pl.xaxis * (h * 0.3) - pl.yaxis * (h * 0.5));
    ctx.Doc().BeginChange("DimOrdinate");
    AddAnnotationGroup(ctx, "DimOrdinate", curves, g);
    ++made_;
    ctx.Print("DimOrdinate: " + g.text);
    WantPoint("Feature point (Enter to finish)");
  }
  void OnEnter(CommandContext& ctx) override { ctx.ClearPreview(); ctx.Print("DimOrdinate: " + std::to_string(made_) + " ordinate(s)"); Finish(); }
  void OnHover(CommandContext& ctx, Point3d h) override { if (base_) { ctx.ClearPreview(); ctx.AddPreviewLine(*base_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::optional<Point3d> base_;
  char dir_ = 'X';
  int made_ = 0;
};

// ---------------------------------------------------------------------------
// DimCreaseAngle: angle between two lines, or two planar faces (normals).
// ---------------------------------------------------------------------------

bool DirectionOf(const SceneObject& o, Vector3d& out, bool& is_normal) {
  is_normal = false;
  if (o.kind == ObjectKind::Curve && o.curve) {
    const kernel::Interval d = o.curve->Domain();
    out = o.curve->PointAt(d.max) - o.curve->PointAt(d.min);
    if (out.Length() < 1e-12) out = o.curve->TangentAt(d.min);
    return out.Unitize();
  }
  ON_Plane pl;
  if (o.kind == ObjectKind::Surface && o.surface && o.surface->raw().IsPlanar(&pl)) { out = pl.zaxis; is_normal = true; return true; }
  if (o.kind == ObjectKind::Brep && o.brep) {
    for (int i = 0; i < o.brep->raw().m_F.Count(); ++i) {
      const ON_Surface* s = o.brep->raw().m_F[i].SurfaceOf();
      if (s && s->IsPlanar(&pl)) { out = pl.zaxis; is_normal = true; return true; }
    }
  }
  if (o.kind == ObjectKind::Mesh && o.mesh && o.mesh->FaceCount() > 0) {
    const ON_Mesh& m = o.mesh->raw();
    const ON_MeshFace& f = m.m_F[0];
    const ON_3dPoint a = m.m_V[f.vi[0]], b = m.m_V[f.vi[1]], c = m.m_V[f.vi[2]];
    out = ON_CrossProduct(b - a, c - a);
    is_normal = true;
    return out.Unitize();
  }
  return false;
}

class DimCreaseAngleCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select two lines or two planar faces", 2); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    std::vector<Vector3d> dirs;
    std::vector<Point3d> centers;
    bool normal = false;
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      Vector3d d;
      bool n = false;
      if (o && DirectionOf(*o, d, n)) { dirs.push_back(d); centers.push_back(CenterOf(*o)); normal = normal || n; }
      if (dirs.size() == 2) break;
    }
    if (dirs.size() < 2) { ctx.Warn("Select two lines or two planar faces"); Finish(); return; }
    double c = std::clamp(ON_DotProduct(dirs[0], dirs[1]), -1.0, 1.0);
    angle_ = std::acos(c) * 180.0 / ON_PI;
    anchor_ = (centers[0] + centers[1]) / 2.0;
    normal_ = normal;
    WantPoint("Dimension location");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.ClearPreview();
    const std::string text = FormatNumber(angle_) + " deg" + (normal_ ? " (crease " + FormatNumber(180.0 - angle_) + " deg)" : "");
    ctx.Doc().BeginChange("DimCreaseAngle");
    AddLeaderText(ctx, "DimCreaseAngle", anchor_, p, text);
    ctx.Print("DimCreaseAngle: " + text);
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { ctx.ClearPreview(); ctx.AddPreviewLine(anchor_, h); }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  double angle_ = 0;
  bool normal_ = false;
  Point3d anchor_;
};

// ---------------------------------------------------------------------------
// Centermark, Arrowhead
// ---------------------------------------------------------------------------

class CentermarkCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    size_ = std::atof(OptionOr(TakeOptionTokens(ctx), "size", "0").c_str());
    options = {{"Size", FormatNumber(size_), {}, true, false}};
    WantObjects("Select circles or arcs");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Size") { size_ = std::max(0.0, std::atof(v.c_str())); options[0].value = FormatNumber(size_); } }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    struct Mark { ON_Arc arc; int layer; };
    std::vector<Mark> marks;
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      ON_Arc arc;
      if (o && o->kind == ObjectKind::Curve && o->curve->raw().IsArc(nullptr, &arc)) marks.push_back({arc, o->layer_index});
    }
    if (marks.empty()) { ctx.Warn("Select circles or arcs"); Finish(); return; }
    ctx.Doc().BeginChange("Centermark");
    for (const Mark& m : marks) {
      const double s = size_ > 0 ? size_ : std::max(m.arc.Radius() * 0.25, AnnotationTextHeight(ctx) * 0.5);
      const ON_Plane& pl = m.arc.plane;
      const Point3d c = m.arc.Center();
      std::vector<kernel::NurbsCurve> curves = {PolylineCurve({c - pl.xaxis * s, c + pl.xaxis * s}), PolylineCurve({c - pl.yaxis * s, c + pl.yaxis * s})};
      AddAnnotationGroup(ctx, "Centermark", curves, GlyphSpec{}, m.layer);
    }
    ctx.Print("Centermark: " + std::to_string(marks.size()) + " center mark(s)");
    Finish();
  }
  double size_ = 0;
};

class ArrowheadCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    end_ = OptionOr(opts, "end", "End");
    size_ = std::atof(OptionOr(opts, "size", "0").c_str());
    options = {{"End", end_, {"Start", "End", "Both"}, false, false}, {"Size", FormatNumber(size_), {}, true, false}};
    WantObjects("Select curves for arrowheads");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "End") { end_ = v; options[0].value = v; }
    if (n == "Size") { size_ = std::max(0.0, std::atof(v.c_str())); options[1].value = FormatNumber(size_); }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    const ON_Plane pl = ActivePlane(ctx);
    const double s = size_ > 0 ? size_ : AnnotationArrowSize(ctx);
    const std::string e = LowerStr(end_);
    int made = 0;
    ctx.Doc().BeginChange("Arrowhead");
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind != ObjectKind::Curve) continue;
      const kernel::Interval d = o->curve->Domain();
      std::vector<kernel::NurbsCurve> curves;
      if (e == "start" || e == "both") curves.push_back(ArrowCurve(o->curve->PointAt(d.min), -o->curve->TangentAt(d.min), s, pl));
      if (e == "end" || e == "both") curves.push_back(ArrowCurve(o->curve->PointAt(d.max), o->curve->TangentAt(d.max), s, pl));
      if (curves.empty()) continue;
      const int layer = o->layer_index;
      AddAnnotationGroup(ctx, "Arrowhead", curves, GlyphSpec{}, layer);
      ++made;
    }
    ctx.Print("Arrowhead: " + std::to_string(made) + " curve(s), " + end_ + ", size " + FormatNumber(s));
    Finish();
  }
  std::string end_ = "End";
  double size_ = 0;
};

// ---------------------------------------------------------------------------
// RevCloud: a closed curve of outward arcs around picked points or a rectangle.
// ---------------------------------------------------------------------------

class RevCloudCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    arc_ = std::atof(OptionOr(opts, "arclength", "0").c_str());
    if (arc_ <= 0) arc_ = AnnotationTextHeight(ctx) * 2;
    rectangle_ = YesLike(OptionOr(opts, "rectangle", "No"));
    options = {{"ArcLength", FormatNumber(arc_), {}, true, false}, {"Rectangle", rectangle_ ? "Yes" : "No", {}, false, true}};
    WantPoint(rectangle_ ? "First corner" : "First point of cloud");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "ArcLength") { double a = std::atof(v.c_str()); if (a > 0) arc_ = a; options[0].value = FormatNumber(arc_); }
    if (n == "Rectangle") { rectangle_ = YesLike(v); options[1].value = rectangle_ ? "Yes" : "No"; }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (rectangle_) {
      if (pts_.size() == 2) { Build(ctx); return; }
      WantPoint("Other corner");
      return;
    }
    WantPoint("Next point (Enter to close the cloud)");
  }
  void OnEnter(CommandContext& ctx) override {
    if (!rectangle_ && pts_.size() >= 3) Build(ctx);
    else { ctx.Warn("RevCloud needs at least 3 points"); ctx.ClearPreview(); Finish(); }
  }
  void Build(CommandContext& ctx) {
    ctx.ClearPreview();
    const ON_Plane pl = ActivePlane(ctx);
    std::vector<Point3d> poly = pts_;
    if (rectangle_) {
      double u0, v0, u1, v1;
      pl.ClosestPointTo(pts_[0], &u0, &v0); pl.ClosestPointTo(pts_[1], &u1, &v1);
      poly = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)};
    }
    // Orientation in the CPlane decides which side is "outward".
    double area2 = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
      double ua, va, ub, vb;
      pl.ClosestPointTo(poly[i], &ua, &va); pl.ClosestPointTo(poly[(i + 1) % poly.size()], &ub, &vb);
      area2 += ua * vb - ub * va;
    }
    const double sign = area2 >= 0 ? 1.0 : -1.0;  // CCW: outward is to the right of the edge
    ON_PolyCurve* pc = new ON_PolyCurve();
    int arcs = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
      const Point3d a = poly[i], b = poly[(i + 1) % poly.size()];
      const double len = a.DistanceTo(b);
      if (len < 1e-9) continue;
      const int n = std::max(1, static_cast<int>(std::ceil(len / arc_)));
      Vector3d dir = b - a; dir.Unitize();
      Vector3d out = ON_CrossProduct(dir, pl.zaxis) * sign; out.Unitize();
      for (int k = 0; k < n; ++k) {
        const Point3d p = a + (b - a) * (static_cast<double>(k) / n), q = a + (b - a) * (static_cast<double>(k + 1) / n);
        const Point3d m = (p + q) / 2.0 + out * (p.DistanceTo(q) * 0.35);
        ON_Arc arc(p, m, q);
        if (!arc.IsValid()) continue;
        pc->Append(new ON_ArcCurve(arc));
        ++arcs;
      }
    }
    kernel::NurbsCurve k;
    const bool ok = arcs > 0 && CurveFromON(*pc, k);
    delete pc;
    if (!ok) { ctx.Warn("RevCloud: could not build the cloud"); Finish(); return; }
    ctx.Doc().BeginChange("RevCloud");
    SceneObject s = SceneObject::MakeCurve(k);
    s.layer_index = DimensionLayer(ctx);
    TagAnnotation(s, "RevCloud", ctx.Settings().annotation_style);
    const ObjectId id = ctx.Doc().Add(std::move(s));
    ctx.Doc().CreateGroup({id}, "RevCloud");
    ctx.Print("RevCloud: " + std::to_string(arcs) + " arc(s), closed " + (k.IsClosed() ? "curve" : "polycurve"));
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    if (pts_.empty()) return;
    if (rectangle_) { ON_Plane pl = ActivePlane(ctx); double u0, v0, u1, v1; pl.ClosestPointTo(pts_[0], &u0, &v0); pl.ClosestPointTo(h, &u1, &v1); ctx.AddPreviewPolyline({pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)}, true); }
    else { std::vector<Point3d> pv = pts_; pv.push_back(h); ctx.AddPreviewPolyline(pv, true); }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<Point3d> pts_;
  double arc_ = 2;
  bool rectangle_ = false;
};

// ---------------------------------------------------------------------------
// Text editing: TextProperties / RTextEdit / RLeaderEdit, MatchAnnotation,
// ScaleTextHeight, FindText, DimRecenterText
// ---------------------------------------------------------------------------

// Rebuilds the text of every annotation group among `ids` through `edit`.
int EditGroups(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::string& label,
               const std::function<void(GlyphSpec&)>& edit) {
  const std::vector<int> groups = AnnotationGroupsOf(ctx, ids);
  int done = 0;
  ctx.Doc().BeginChange(label);
  for (int g : groups) {
    GlyphSpec spec;
    if (!GroupGlyphSpec(ctx, g, spec)) continue;
    edit(spec);
    if (spec.text.empty() || spec.height <= 0) continue;
    if (RebuildGroupText(ctx, g, spec) > 0) ++done;
  }
  return done;
}

class TextPropertiesCommand : public Command {
 public:
  explicit TextPropertiesCommand(std::string label) : label_(std::move(label)) {}
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    text_ = OptionOr(opts, "text");
    height_ = std::atof(OptionOr(opts, "height", "0").c_str());
    options = {{"Text", text_, {}, false, false}, {"Height", FormatNumber(height_), {}, true, false}};
    WantObjects("Select annotations to edit");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Text") { text_ = v; options[0].value = v; }
    if (n == "Height") { height_ = std::max(0.0, std::atof(v.c_str())); options[1].value = FormatNumber(height_); }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    if (AnnotationGroupsOf(ctx, ids).empty()) { ctx.Warn("Select text or dimension annotations"); Finish(); return; }
    if (text_.empty() && height_ <= 0) {
      GlyphSpec cur;
      std::string def;
      if (GroupGlyphSpec(ctx, AnnotationGroupsOf(ctx, ids).front(), cur)) def = cur.text;
      WantText("New text (Enter keeps it; Height=<n> changes the height)", def);
      return;
    }
    Apply(ctx);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { text_ = t; Apply(ctx); }
  void OnEnter(CommandContext& ctx) override { Apply(ctx); }
  void Apply(CommandContext& ctx) {
    const int n = EditGroups(ctx, ids_, label_, [&](GlyphSpec& g) { if (!text_.empty()) g.text = text_; if (height_ > 0) g.height = height_; });
    ctx.Print(label_ + ": " + std::to_string(n) + " annotation(s) updated" + (text_.empty() ? "" : " to \"" + text_ + "\"") + (height_ > 0 ? ", height " + FormatNumber(height_) : ""));
    Finish();
  }
  std::string label_, text_;
  double height_ = 0;
  std::vector<ObjectId> ids_;
};

class MatchAnnotationCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select the source annotation"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!have_source_) {
      const std::vector<int> groups = AnnotationGroupsOf(ctx, ids);
      if (groups.empty() || !GroupGlyphSpec(ctx, groups.front(), source_)) { ctx.Warn("Select a text or dimension annotation as the source"); Finish(); return; }
      source_group_ = groups.front();
      for (const SceneObject& o : ctx.Doc().Objects()) if (o.group_id == source_group_) { auto it = o.user_text.find("Style"); if (it != o.user_text.end()) style_ = it->second; break; }
      have_source_ = true;
      ctx.Doc().SelectNone();
      WantObjects("Select annotations to change");
      return;
    }
    std::vector<ObjectId> targets;
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->group_id != source_group_) targets.push_back(id); }
    const std::vector<int> groups = AnnotationGroupsOf(ctx, targets);
    const int n = EditGroups(ctx, targets, "MatchAnnotation", [&](GlyphSpec& g) { g.height = source_.height; });
    for (SceneObject& o : ctx.Doc().Objects()) if (std::find(groups.begin(), groups.end(), o.group_id) != groups.end() && !style_.empty()) o.user_text["Style"] = style_;
    ctx.Print("MatchAnnotation: " + std::to_string(n) + " annotation(s) matched (height " + FormatNumber(source_.height) + (style_.empty() ? "" : ", style " + style_) + ")");
    Finish();
  }
  bool have_source_ = false;
  int source_group_ = -1;
  GlyphSpec source_;
  std::string style_;
};

class ScaleTextHeightCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    factor_ = std::atof(OptionOr(TakeOptionTokens(ctx), "factor", "0").c_str());
    options = {{"Factor", FormatNumber(factor_), {}, true, false}};
    WantObjects("Select annotations to scale");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Factor") { factor_ = std::atof(v.c_str()); options[0].value = FormatNumber(factor_); } }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    if (factor_ > 0) { Apply(ctx); return; }
    WantNumber("Scale factor", 2.0);
  }
  void OnNumber(CommandContext& ctx, double v) override { factor_ = v; Apply(ctx); }
  void Apply(CommandContext& ctx) {
    if (factor_ <= 0) { ctx.Warn("Scale factor must be positive"); Finish(); return; }
    const int n = EditGroups(ctx, ids_, "ScaleTextHeight", [&](GlyphSpec& g) { g.height *= factor_; });
    ctx.Print("ScaleTextHeight: " + std::to_string(n) + " annotation(s) scaled by " + FormatNumber(factor_));
    Finish();
  }
  double factor_ = 0;
  std::vector<ObjectId> ids_;
};

class FindTextCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    text_ = OptionOr(TakeOptionTokens(ctx), "text");
    if (!text_.empty()) { Apply(ctx); return; }
    WantText("Text to find");
  }
  void OnText(CommandContext& ctx, const std::string& t) override { text_ = t; Apply(ctx); }
  void Apply(CommandContext& ctx) {
    const std::string needle = LowerStr(text_);
    std::vector<int> groups;
    for (const SceneObject& o : ctx.Doc().Objects()) {
      auto it = o.user_text.find("Text");
      if (it == o.user_text.end() || o.group_id < 0) continue;
      if (LowerStr(it->second).find(needle) != std::string::npos && std::find(groups.begin(), groups.end(), o.group_id) == groups.end()) groups.push_back(o.group_id);
    }
    ctx.Doc().SelectWhere([&](const SceneObject& o) { return std::find(groups.begin(), groups.end(), o.group_id) != groups.end(); });
    ctx.Print("FindText: " + std::to_string(groups.size()) + " annotation(s) containing \"" + text_ + "\" selected");
    Finish();
  }
  std::string text_;
};

// ---------------------------------------------------------------------------
// Annotation styles
// ---------------------------------------------------------------------------

std::string StyleSummary(const AnnotationStyle& s) {
  return s.name + " (text height " + (s.text_height > 0 ? FormatNumber(s.text_height) : std::string("auto")) + ", arrow " + (s.arrow_size > 0 ? FormatNumber(s.arrow_size) : std::string("auto")) + (s.font.empty() ? "" : ", font " + s.font) + ")";
}

void AnnotationStylesCommand(CommandContext& ctx) {
  auto opts = TakeOptionTokens(ctx);
  const std::string name = OptionOr(opts, "name");
  if (name.empty()) {
    for (const AnnotationStyle& s : ctx.Doc().AnnotationStyles()) ctx.Print(std::string(s.name == ctx.Settings().annotation_style ? "* " : "  ") + StyleSummary(s));
    ctx.App().Panels().document_properties = true;
    return;
  }
  AnnotationStyle* st = ctx.Doc().FindAnnotationStyle(name);
  if (!st) { ctx.Doc().AnnotationStyles().push_back(AnnotationStyle{name, 0, 0, ""}); st = &ctx.Doc().AnnotationStyles().back(); }
  if (opts.count("height")) st->text_height = std::max(0.0, std::atof(opts["height"].c_str()));
  if (opts.count("arrow")) st->arrow_size = std::max(0.0, std::atof(opts["arrow"].c_str()));
  if (opts.count("font")) st->font = opts["font"];
  if (!opts.count("current") || YesLike(opts["current"])) ctx.Settings().annotation_style = name;
  ctx.Doc().Touch();
  ctx.Print("AnnotationStyles: " + StyleSummary(*st) + (ctx.Settings().annotation_style == name ? " is current" : ""));
}

void DupAnnotationStyle(CommandContext& ctx) {
  auto opts = TakeOptionTokens(ctx);
  const std::string src = OptionOr(opts, "name", ctx.Settings().annotation_style);
  const AnnotationStyle* s = ctx.Doc().FindAnnotationStyle(src);
  if (!s) { ctx.Warn("No annotation style named '" + src + "'"); return; }
  AnnotationStyle copy = *s;
  copy.name = OptionOr(opts, "newname", src + " copy");
  if (ctx.Doc().FindAnnotationStyle(copy.name)) *ctx.Doc().FindAnnotationStyle(copy.name) = copy; else ctx.Doc().AnnotationStyles().push_back(copy);
  ctx.Doc().Touch();
  ctx.Print("DupAnnotationStyle: '" + src + "' duplicated as '" + copy.name + "'");
}

void ImportAnnotationStyles(CommandContext& ctx) {
  const std::string path = OptionOr(TakeOptionTokens(ctx), "path");
  if (path.empty()) { ctx.Warn("ImportAnnotationStyles Path=<file.3dm>"); return; }
  Document other;
  std::string err;
  if (!Load3dm(other, path, err)) { ctx.Warn(err.empty() ? "Could not read " + path : err); return; }
  int n = 0;
  for (const AnnotationStyle& s : other.AnnotationStyles()) {
    if (AnnotationStyle* mine = ctx.Doc().FindAnnotationStyle(s.name)) *mine = s; else ctx.Doc().AnnotationStyles().push_back(s);
    ++n;
  }
  ctx.Doc().Touch();
  ctx.Print("ImportAnnotationStyles: " + std::to_string(n) + " style(s) from " + path);
}

void SelAnnotationStyle(CommandContext& ctx) {
  const std::string name = OptionOr(TakeOptionTokens(ctx), "name", ctx.Settings().annotation_style);
  ctx.Doc().SelectWhere([&](const SceneObject& o) { auto it = o.user_text.find("Style"); return o.user_text.count("Annotation") && it != o.user_text.end() && it->second == name; });
  ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) with annotation style '" + name + "' selected");
}

// ---------------------------------------------------------------------------
// Dot / ConvertDots / ConvertTextToBlockAttribute / SetDimensionLayer
// ---------------------------------------------------------------------------

class DotCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantText("Dot text"); }
  void OnText(CommandContext&, const std::string& t) override { if (text_.empty()) { text_ = t; WantPoint("Location of dot"); } }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    SceneObject o = SceneObject::MakePoint(p);
    o.user_text["Dot"] = text_;
    o.name = text_;
    AddObject(ctx, std::move(o), "Dot");
    ctx.Print("Dot \"" + text_ + "\" at " + FormatPoint(p));
    Finish();
  }
  std::string text_;
};

void ConvertDots(CommandContext& ctx) {
  std::vector<ObjectId> dots;
  const bool any_selected = ctx.Doc().SelectedCount() > 0;
  for (const SceneObject& o : ctx.Doc().Objects()) if (o.kind == ObjectKind::Point && o.user_text.count("Dot") && (!any_selected || o.selected)) dots.push_back(o.id);
  if (dots.empty()) { ctx.Warn("No dots to convert (Dot creates them)"); return; }
  ctx.Doc().BeginChange("ConvertDots");
  const ON_Plane base = ActivePlane(ctx);
  const double h = AnnotationTextHeight(ctx);
  int made = 0;
  for (ObjectId id : dots) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    GlyphSpec g;
    g.text = o->user_text.at("Dot"); g.height = h; g.plane = base; g.plane.SetOrigin(o->point); g.center = false;
    const int layer = o->layer_index;
    std::vector<ObjectId> ids = AddGlyphCurves(ctx, g, layer, -1, {{"Annotation", "Text"}, {"Style", ctx.Settings().annotation_style}});
    if (ids.empty()) continue;
    ctx.Doc().CreateGroup(ids, "Text");
    ctx.Doc().Remove(id);
    ++made;
  }
  ctx.Print("ConvertDots: " + std::to_string(made) + " dot(s) converted to text");
}

class ConvertTextToBlockAttributeCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override { key_ = OptionOr(TakeOptionTokens(ctx), "key", "Attribute"); WantObjects("Select text to convert to block attributes"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    const std::vector<int> groups = AnnotationGroupsOf(ctx, ids);
    ctx.Doc().BeginChange("ConvertTextToBlockAttribute");
    int n = 0;
    for (int g : groups) {
      GlyphSpec spec;
      if (!GroupGlyphSpec(ctx, g, spec)) continue;
      for (SceneObject& o : ctx.Doc().Objects()) if (o.group_id == g) { o.user_text[key_] = spec.text; o.user_text["BlockAttribute"] = key_; }
      ++n;
    }
    ctx.Print("ConvertTextToBlockAttribute: " + std::to_string(n) + " text(s) tagged as attribute '" + key_ + "'");
    Finish();
  }
  std::string key_;
};

class SetDimensionLayerCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    const std::string l = OptionOr(TakeOptionTokens(ctx), "layer");
    if (!l.empty()) { Apply(ctx, l); return; }
    WantText("Layer for new dimensions (empty = current layer)", ctx.Settings().dimension_layer.empty() ? ctx.Doc().LayerFullPath(ctx.Doc().CurrentLayer()) : ctx.Settings().dimension_layer);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { Apply(ctx, t); }
  void Apply(CommandContext& ctx, const std::string& name) {
    if (!name.empty() && ctx.Doc().FindLayer(name) < 0) { ctx.Doc().BeginChange("SetDimensionLayer"); ctx.Doc().AddLayer(name); }
    ctx.Settings().dimension_layer = name;
    ctx.Doc().Touch();
    ctx.Print("SetDimensionLayer: new dimensions go on layer " + (name.empty() ? std::string("(current)") : name));
    Finish();
  }
};

// ---------------------------------------------------------------------------
// Hatch extras: HatchBase, HatchScale
// ---------------------------------------------------------------------------

class HatchScaleCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    factor_ = std::atof(OptionOr(opts, "factor", "0").c_str());
    spacing_ = std::atof(OptionOr(opts, "spacing", "0").c_str());
    options = {{"Factor", FormatNumber(factor_), {}, true, false}, {"Spacing", FormatNumber(spacing_), {}, true, false}};
    WantObjects("Select hatches to rescale");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Factor") { factor_ = std::atof(v.c_str()); options[0].value = FormatNumber(factor_); }
    if (n == "Spacing") { spacing_ = std::atof(v.c_str()); options[1].value = FormatNumber(spacing_); }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    if (factor_ > 0 || spacing_ > 0) { Apply(ctx); return; }
    WantNumber("Scale factor", 2.0);
  }
  void OnNumber(CommandContext& ctx, double v) override { factor_ = v; Apply(ctx); }
  void Apply(CommandContext& ctx) {
    std::vector<int> groups;
    for (ObjectId id : ids_) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->group_id >= 0 && o->user_text.count("Hatch") && std::find(groups.begin(), groups.end(), o->group_id) == groups.end()) groups.push_back(o->group_id); }
    if (groups.empty()) { ctx.Warn("Select hatches (made by Hatch)"); Finish(); return; }
    ctx.Doc().BeginChange("HatchScale");
    int rebuilt = 0, scaled = 0;
    for (int g : groups) {
      std::vector<ObjectId> members = ctx.Doc().GroupMembers(g);
      const SceneObject* first = ctx.Doc().Find(members.front());
      if (!first) continue;
      auto tag = [&](const char* k, const std::string& def) { auto it = first->user_text.find(k); return it == first->user_text.end() ? def : it->second; };
      const int pattern = HatchPatternIndex(tag("Hatch", "Hatch1"));
      const double old_spacing = std::atof(tag("HatchSpacing", "1").c_str());
      const double rotation = std::atof(tag("HatchRotation", "45").c_str());
      const double spacing = spacing_ > 0 ? spacing_ : old_spacing * factor_;
      const ObjectId boundary_id = static_cast<ObjectId>(std::strtoull(tag("HatchBoundary", "0").c_str(), nullptr, 10));
      const int layer = first->layer_index;
      const SceneObject* boundary = boundary_id ? ctx.Doc().Find(boundary_id) : nullptr;
      if (boundary && boundary->kind == ObjectKind::Curve && boundary->curve->IsClosed() && pattern != 0) {
        const kernel::NurbsCurve bc = *boundary->curve;
        for (ObjectId id : members) ctx.Doc().Remove(id);
        AddHatch(ctx, bc, boundary_id, layer, pattern, spacing, rotation);
        ++rebuilt;
      } else {
        // No boundary any more: scale the pattern about the hatch centre.
        const double f = old_spacing > 0 ? spacing / old_spacing : factor_;
        if (f <= 0 || pattern == 0) continue;
        kernel::BoundingBox bb;
        if (!ctx.Doc().BoundingBoxOf(members, bb)) continue;
        const ON_Xform xf = ON_Xform::ScaleTransformation((bb.min + bb.max) / 2.0, f);
        for (ObjectId id : members) if (SceneObject* o = ctx.Doc().Find(id)) { o->Transform(xf); o->user_text["HatchSpacing"] = FormatNumber(spacing); }
        ++scaled;
      }
    }
    ctx.Print("HatchScale: " + std::to_string(rebuilt) + " hatch(es) rebuilt, " + std::to_string(scaled) + " scaled" + (spacing_ > 0 ? ", spacing " + FormatNumber(spacing_) : ", factor " + FormatNumber(factor_)));
    Finish();
  }
  double factor_ = 0, spacing_ = 0;
  std::vector<ObjectId> ids_;
};

class HatchBaseCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override { WantPoint("Hatch pattern base point <" + FormatPoint(ctx.Settings().hatch_base) + ">"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.Settings().hatch_base = p;
    ctx.Doc().Touch();
    ctx.Print("HatchBase: " + FormatPoint(p));
    Finish();
  }
  void OnEnter(CommandContext& ctx) override { ctx.Print("HatchBase: " + FormatPoint(ctx.Settings().hatch_base)); Finish(); }
};

// ---------------------------------------------------------------------------
// Linetypes
// ---------------------------------------------------------------------------

std::string PatternText(const std::vector<double>& p) {
  std::string s;
  for (double d : p) s += (s.empty() ? "" : ",") + FormatNumber(d);
  return s.empty() ? "continuous" : s;
}

bool ParsePattern(const std::string& text, std::vector<double>& out) {
  out.clear();
  std::string t = text;
  for (char& c : t) if (c == ';' || c == ' ') c = ',';
  std::istringstream ss(t);
  std::string tok;
  while (std::getline(ss, tok, ',')) {
    if (tok.empty()) continue;
    char* end = nullptr;
    const double v = std::strtod(tok.c_str(), &end);
    if (!end || *end != 0 || v < 0) return false;
    out.push_back(v);
  }
  return true;
}

void ListLinetypes(CommandContext& ctx) {
  for (const Linetype& l : ctx.Doc().Linetypes()) ctx.Print("Linetype " + l.name + ": " + PatternText(l.pattern));
  ctx.Print("Linetype scale " + FormatNumber(ctx.Settings().linetype_scale) + ", display " + (ctx.Settings().linetype_display ? "on" : "off"));
  ctx.App().Panels().linetypes = true;
}

// Resolves a typed linetype name against the table (case-insensitive).
std::optional<std::string> ResolveLinetype(CommandContext& ctx, const std::string& name, bool allow_by_layer) {
  if (allow_by_layer && LowerStr(name) == "bylayer") return std::string("ByLayer");
  for (const Linetype& l : ctx.Doc().Linetypes()) if (LowerStr(l.name) == LowerStr(name)) return l.name;
  return std::nullopt;
}

class SetLinetypeCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override { name_ = OptionOr(TakeOptionTokens(ctx), "name"); WantObjects("Select objects to change linetype"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    if (!name_.empty()) { Apply(ctx); return; }
    std::string names = "ByLayer";
    for (const Linetype& l : ctx.Doc().Linetypes()) names += ", " + l.name;
    ctx.Print("Linetypes: " + names);
    WantText("Linetype name", "ByLayer");
  }
  void OnText(CommandContext& ctx, const std::string& t) override { name_ = t; Apply(ctx); }
  void Apply(CommandContext& ctx) {
    std::optional<std::string> lt = ResolveLinetype(ctx, name_, true);
    if (!lt) { ctx.Warn("No linetype named '" + name_ + "' (SetCustomLinetype creates one)"); Finish(); return; }
    ctx.Doc().BeginChange("SetLinetype");
    int n = 0;
    for (ObjectId id : ids_) if (SceneObject* o = ctx.Doc().Find(id)) { o->linetype = *lt; o->InvalidateDisplay(); ++n; }
    ctx.Print("SetLinetype: " + std::to_string(n) + " object(s) set to " + *lt);
    Finish();
  }
  std::string name_;
  std::vector<ObjectId> ids_;
};

class SetLayerLinetypeCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    name_ = OptionOr(opts, "name");
    layer_ = OptionOr(opts, "layer");
    if (!name_.empty()) { Apply(ctx); return; }
    WantText("Linetype for layer " + (layer_.empty() ? ctx.Doc().LayerFullPath(ctx.Doc().CurrentLayer()) : layer_), "Continuous");
  }
  void OnText(CommandContext& ctx, const std::string& t) override { name_ = t; Apply(ctx); }
  void Apply(CommandContext& ctx) {
    const int idx = layer_.empty() ? ctx.Doc().CurrentLayer() : ctx.Doc().FindLayer(layer_);
    if (idx < 0) { ctx.Warn("No layer named '" + layer_ + "'"); Finish(); return; }
    std::optional<std::string> lt = ResolveLinetype(ctx, name_, false);
    if (!lt) { ctx.Warn("No linetype named '" + name_ + "'"); Finish(); return; }
    ctx.Doc().BeginChange("SetLayerLinetype");
    ctx.Doc().Layers()[static_cast<size_t>(idx)].linetype = *lt;
    for (SceneObject& o : ctx.Doc().Objects()) if (o.layer_index == idx) o.InvalidateDisplay();
    ctx.Print("SetLayerLinetype: layer " + ctx.Doc().LayerFullPath(idx) + " uses " + *lt);
    Finish();
  }
  std::string name_, layer_;
};

class SetLinetypeScaleCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    const double s = std::atof(OptionOr(TakeOptionTokens(ctx), "scale", "0").c_str());
    if (s > 0) { Apply(ctx, s); return; }
    WantNumber("Linetype scale", ctx.Settings().linetype_scale);
  }
  void OnNumber(CommandContext& ctx, double v) override { Apply(ctx, v); }
  void Apply(CommandContext& ctx, double s) {
    if (s <= 0) { ctx.Warn("Scale must be positive"); Finish(); return; }
    ctx.Settings().linetype_scale = s;
    ctx.Doc().Touch();
    ctx.Print("SetLinetypeScale: " + FormatNumber(s));
    Finish();
  }
};

void LinetypeDisplay(CommandContext& ctx) {
  auto opts = TakeOptionTokens(ctx);
  bool on = !ctx.Settings().linetype_display;
  if (opts.count("enable")) on = YesLike(opts["enable"]);
  ctx.Settings().linetype_display = on;
  ctx.Doc().Touch();
  ctx.Print(std::string("LinetypeDisplay: ") + (on ? "on" : "off"));
}

class SetCustomLinetypeCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    name_ = OptionOr(opts, "name");
    pattern_ = OptionOr(opts, "pattern");
    if (!name_.empty() && !pattern_.empty()) { Apply(ctx); return; }
    if (name_.empty()) WantText("Linetype name"); else WantText("Dash pattern (dash,gap,... in model units)", "5,2");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (name_.empty()) { name_ = t; WantText("Dash pattern (dash,gap,... in model units)", "5,2"); return; }
    pattern_ = t;
    Apply(ctx);
  }
  void Apply(CommandContext& ctx) {
    std::vector<double> p;
    if (!ParsePattern(pattern_, p)) { ctx.Warn("Pattern must be numbers separated by commas, e.g. 5,2,1,2"); Finish(); return; }
    ctx.Doc().SetLinetype(name_, p);
    ctx.Print("SetCustomLinetype: " + name_ + " = " + PatternText(p));
    Finish();
  }
  std::string name_, pattern_;
};

class ExtractLineTypeSegmentsCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    delete_ = YesLike(OptionOr(TakeOptionTokens(ctx), "deleteinput", "No"));
    options = {{"DeleteInput", delete_ ? "Yes" : "No", {}, false, true}};
    WantObjects("Select curves with a dashed linetype");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "DeleteInput") { delete_ = YesLike(v); options[0].value = delete_ ? "Yes" : "No"; } }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    struct Src { ObjectId id; std::vector<std::vector<Point3d>> dashes; int layer; };
    std::vector<Src> sources;
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind != ObjectKind::Curve) continue;
      const Linetype* lt = ctx.Doc().FindLinetype(ctx.Doc().EffectiveLinetype(*o));
      if (!lt || lt->pattern.empty()) continue;
      std::vector<double> pattern;
      for (double d : lt->pattern) pattern.push_back(d * std::max(1e-9, ctx.Settings().linetype_scale));
      std::vector<double> params = o->curve->SuggestedParameterValues(std::min(0.01, ctx.App().curve_display_tolerance), 10);
      if (params.size() < 8) { params.clear(); const kernel::Interval d = o->curve->Domain(); for (int i = 0; i <= 24; ++i) params.push_back(d.min + (d.max - d.min) * i / 24.0); }
      std::vector<Point3d> pts;
      for (double t : params) pts.push_back(o->curve->PointAt(t));
      sources.push_back({id, DashPolyline(pts, pattern), o->layer_index});
    }
    if (sources.empty()) { ctx.Warn("Select curves whose linetype is dashed (SetLinetype)"); Finish(); return; }
    ctx.Doc().BeginChange("ExtractLineTypeSegments");
    int total = 0;
    for (const Src& s : sources) {
      std::vector<ObjectId> out;
      for (const std::vector<Point3d>& d : s.dashes) {
        if (d.size() < 2) continue;
        SceneObject n = SceneObject::MakeCurve(PolylineCurve(d));
        n.layer_index = s.layer;
        n.linetype = "Continuous";
        out.push_back(ctx.Doc().Add(std::move(n)));
      }
      if (!out.empty()) ctx.Doc().CreateGroup(out, "LinetypeSegments");
      total += static_cast<int>(out.size());
      if (delete_) ctx.Doc().Remove(s.id);
    }
    ctx.Print("ExtractLineTypeSegments: " + std::to_string(total) + " segment(s) from " + std::to_string(sources.size()) + " curve(s)");
    Finish();
  }
  bool delete_ = false;
};

class SelLinetypeCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    name_ = OptionOr(TakeOptionTokens(ctx), "name");
    if (name_.empty()) for (const SceneObject& o : ctx.Doc().Objects()) if (o.selected) { name_ = ctx.Doc().EffectiveLinetype(o); break; }
    if (!name_.empty()) { Apply(ctx); return; }
    WantText("Linetype name", "Continuous");
  }
  void OnText(CommandContext& ctx, const std::string& t) override { name_ = t; Apply(ctx); }
  void Apply(CommandContext& ctx) {
    std::optional<std::string> lt = ResolveLinetype(ctx, name_, false);
    if (!lt) { ctx.Warn("No linetype named '" + name_ + "'"); Finish(); return; }
    ctx.Doc().SelectWhere([&](const SceneObject& o) { return o.kind == ObjectKind::Curve && ctx.Doc().EffectiveLinetype(o) == *lt && ctx.Doc().IsObjectVisible(o) && !ctx.Doc().IsObjectLocked(o); });
    ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " curve(s) with linetype " + *lt + " selected");
    Finish();
  }
  std::string name_;
};

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

struct Instance {
  int group = -1;
  std::string name;
  Point3d insert;
  std::vector<ObjectId> members;
};

// Insertion point of an instance group: the BlockInsert tag, else its centre.
Instance InstanceOf(CommandContext& ctx, int group) {
  Instance inst;
  inst.group = group;
  inst.members = ctx.Doc().GroupMembers(group);
  kernel::BoundingBox bb;
  bool have_tag = false;
  for (ObjectId id : inst.members) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    auto b = o->user_text.find("Block");
    if (b != o->user_text.end() && inst.name.empty()) inst.name = b->second;
    auto t = o->user_text.find("BlockInsert");
    if (t != o->user_text.end() && !have_tag) have_tag = ParsePointTag(t->second, inst.insert);
  }
  if (!have_tag && ctx.Doc().BoundingBoxOf(inst.members, bb)) inst.insert = (bb.min + bb.max) / 2.0;
  return inst;
}

// Distinct block instances among `ids` (or every instance when `ids` is empty).
std::vector<Instance> InstancesAmong(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::string& only_name = "") {
  std::vector<int> groups;
  auto consider = [&](const SceneObject& o) {
    if (o.group_id < 0) return;
    auto it = o.user_text.find("Block");
    if (it == o.user_text.end()) return;
    if (!only_name.empty() && it->second != only_name) return;
    if (std::find(groups.begin(), groups.end(), o.group_id) == groups.end()) groups.push_back(o.group_id);
  };
  if (ids.empty()) for (const SceneObject& o : ctx.Doc().Objects()) consider(o);
  else for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) consider(*o);
  std::vector<Instance> out;
  for (int g : groups) out.push_back(InstanceOf(ctx, g));
  return out;
}

// Replaces every instance of `name` by a fresh copy of its (updated) definition.
int Reinstantiate(CommandContext& ctx, const std::vector<Instance>& instances, const std::string& new_name) {
  int n = 0;
  for (const Instance& inst : instances) {
    for (ObjectId id : inst.members) ctx.Doc().Remove(id);
    if (InstantiateBlock(ctx, new_name, inst.insert) >= 0) ++n;
  }
  return n;
}

std::string BlockNames(CommandContext& ctx) {
  std::string names;
  for (const BlockDefinition& b : ctx.Doc().Blocks()) names += (names.empty() ? "" : ", ") + b.name;
  return names.empty() ? "(none)" : names;
}

class BlockEditCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto& ut = ctx.Doc().UserText();
    if (ut.count("BlockEdit")) { FinishEdit(ctx); return; }
    TakeOptionTokens(ctx);
    WantObjects("Select a block instance to edit");
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    std::vector<Instance> inst = InstancesAmong(ctx, ids);
    if (inst.empty()) { ctx.Warn("Select a block instance"); Finish(); return; }
    const Instance& i = inst.front();
    BlockDefinition* def = ctx.Doc().FindBlock(i.name);
    if (!def) { ctx.Warn("Block '" + i.name + "' has no definition (RescueBlockOrphans)"); Finish(); return; }
    ctx.Doc().BeginChange("BlockEdit");
    const ON_Xform xf = ON_Xform::TranslationTransformation(i.insert - def->base);
    int n = 0;
    for (const SceneObject& o : def->objects) {
      SceneObject c = o;
      c.id = kNoObject; c.group_id = -1; c.selected = true;
      c.Transform(xf);
      c.user_text["BlockEditing"] = i.name;
      ctx.Doc().Add(std::move(c));
      ++n;
    }
    for (ObjectId id : i.members) ctx.Doc().Remove(id);
    ctx.Doc().UserText()["BlockEdit"] = i.name;
    ctx.Doc().UserText()["BlockEditInsert"] = PointTag(i.insert);
    ctx.Print("BlockEdit: editing '" + i.name + "' (" + std::to_string(n) + " object(s) placed at the instance). Edit them, then run BlockEdit again to redefine the block.");
    Finish();
  }
  void FinishEdit(CommandContext& ctx) {
    const std::string name = ctx.Doc().UserText()["BlockEdit"];
    Point3d insert;
    if (!ParsePointTag(ctx.Doc().UserText()["BlockEditInsert"], insert)) insert = Point3d(0, 0, 0);
    BlockDefinition* def = ctx.Doc().FindBlock(name);
    if (!def) { ctx.Doc().UserText().erase("BlockEdit"); ctx.Doc().UserText().erase("BlockEditInsert"); ctx.Warn("Block '" + name + "' no longer exists"); Finish(); return; }
    ctx.Doc().BeginChange("BlockEdit");
    const ON_Xform back = ON_Xform::TranslationTransformation(def->base - insert);
    std::vector<ObjectId> editing;
    std::vector<SceneObject> objs;
    for (const SceneObject& o : ctx.Doc().Objects()) {
      auto it = o.user_text.find("BlockEditing");
      if (it == o.user_text.end() || it->second != name) continue;
      SceneObject c = o;
      c.id = kNoObject; c.group_id = -1; c.selected = false;
      c.user_text.erase("BlockEditing");
      c.Transform(back);
      objs.push_back(std::move(c));
      editing.push_back(o.id);
    }
    def->objects = objs;
    for (ObjectId id : editing) ctx.Doc().Remove(id);
    // Every instance (including the one being edited) picks up the new definition.
    std::vector<Instance> others = InstancesAmong(ctx, {}, name);
    int n = Reinstantiate(ctx, others, name);
    if (InstantiateBlock(ctx, name, insert) >= 0) ++n;
    ctx.Doc().UserText().erase("BlockEdit");
    ctx.Doc().UserText().erase("BlockEditInsert");
    ctx.Print("BlockEdit: block '" + name + "' redefined with " + std::to_string(objs.size()) + " object(s), " + std::to_string(n) + " instance(s) updated");
    Finish();
  }
};

class AddObjectsToBlockCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override { name_ = OptionOr(TakeOptionTokens(ctx), "name"); WantObjects("Select objects to add to a block (include an instance, or give Name=)"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    if (name_.empty()) { std::vector<Instance> inst = InstancesAmong(ctx, ids); if (!inst.empty()) name_ = inst.front().name; }
    if (name_.empty()) { ctx.Print("Blocks: " + BlockNames(ctx)); WantText("Block name"); return; }
    Apply(ctx);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { name_ = t; Apply(ctx); }
  void Apply(CommandContext& ctx) {
    BlockDefinition* def = ctx.Doc().FindBlock(name_);
    if (!def) { ctx.Warn("No block named '" + name_ + "'"); Finish(); return; }
    std::vector<Instance> all = InstancesAmong(ctx, {}, name_);
    // Objects are measured relative to the first instance (or the base point).
    const Point3d ref = all.empty() ? def->base : all.front().insert;
    const ON_Xform xf = ON_Xform::TranslationTransformation(def->base - ref);
    ctx.Doc().BeginChange("AddObjectsToBlock");
    std::vector<ObjectId> added;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->user_text.count("Block")) continue;
      SceneObject c = *o;
      c.id = kNoObject; c.group_id = -1; c.selected = false;
      c.Transform(xf);
      def->objects.push_back(std::move(c));
      added.push_back(id);
    }
    for (ObjectId id : added) ctx.Doc().Remove(id);
    const int n = Reinstantiate(ctx, InstancesAmong(ctx, {}, name_), name_);
    ctx.Print("AddObjectsToBlock: " + std::to_string(added.size()) + " object(s) added to '" + name_ + "', " + std::to_string(n) + " instance(s) updated");
    Finish();
  }
  std::string name_;
  std::vector<ObjectId> ids_;
};

class ReplaceBlockCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override { name_ = OptionOr(TakeOptionTokens(ctx), "name"); WantObjects("Select block instances to replace"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    instances_ = InstancesAmong(ctx, ids);
    if (instances_.empty()) { ctx.Warn("Select block instances"); Finish(); return; }
    if (!name_.empty()) { Apply(ctx); return; }
    ctx.Print("Blocks: " + BlockNames(ctx));
    WantText("Replacement block name");
  }
  void OnText(CommandContext& ctx, const std::string& t) override { name_ = t; if (!instances_.empty()) Apply(ctx); }
  void Apply(CommandContext& ctx) {
    if (!ctx.Doc().FindBlock(name_)) { ctx.Warn("No block named '" + name_ + "'"); Finish(); return; }
    ctx.Doc().BeginChange("ReplaceBlock");
    const int n = Reinstantiate(ctx, instances_, name_);
    ctx.Print("ReplaceBlock: " + std::to_string(n) + " instance(s) now '" + name_ + "'");
    Finish();
  }
  std::string name_;
  std::vector<Instance> instances_;
};

class CreateUniqueBlockCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override { name_ = OptionOr(TakeOptionTokens(ctx), "name"); WantObjects("Select block instances to make unique"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    instances_ = InstancesAmong(ctx, ids);
    if (instances_.empty()) { ctx.Warn("Select block instances"); Finish(); return; }
    if (!name_.empty()) { Apply(ctx); return; }
    WantText("Name for the unique block", instances_.front().name + " unique");
  }
  void OnText(CommandContext& ctx, const std::string& t) override { name_ = t; if (!instances_.empty()) Apply(ctx); }
  void Apply(CommandContext& ctx) {
    if (instances_.empty() || name_.empty()) { ctx.Warn("Select block instances and give a name"); Finish(); return; }
    const std::string src = instances_.front().name;
    const BlockDefinition* def = ctx.Doc().FindBlock(src);
    if (!def) { ctx.Warn("Block '" + src + "' has no definition"); Finish(); return; }
    ctx.Doc().BeginChange("CreateUniqueBlock");
    BlockDefinition copy = *def;
    copy.name = name_;
    if (BlockDefinition* existing = ctx.Doc().FindBlock(name_)) *existing = copy; else ctx.Doc().Blocks().push_back(copy);
    std::vector<Instance> same;
    for (const Instance& i : instances_) if (i.name == src) same.push_back(i);
    const int n = Reinstantiate(ctx, same, name_);
    ctx.Print("CreateUniqueBlock: '" + name_ + "' copied from '" + src + "', " + std::to_string(n) + " instance(s) switched");
    Finish();
  }
  std::string name_;
  std::vector<Instance> instances_;
};

void ExportLinkedBlocks(CommandContext& ctx) {
  auto opts = TakeOptionTokens(ctx);
  std::string name = OptionOr(opts, "name");
  const std::string path = OptionOr(opts, "path");
  if (name.empty()) { std::vector<Instance> inst = InstancesAmong(ctx, ctx.Doc().SelectedIds()); if (!inst.empty()) name = inst.front().name; }
  if (name.empty() || path.empty()) { ctx.Warn("ExportLinkedBlocks Name=<block> Path=<file.3dm> (or select an instance)"); return; }
  BlockDefinition* def = ctx.Doc().FindBlock(name);
  if (!def) { ctx.Warn("No block named '" + name + "'"); return; }
  Document sub;
  sub.Layers() = ctx.Doc().Layers();
  sub.Settings() = ctx.Doc().Settings();
  for (const SceneObject& o : def->objects) { SceneObject c = o; c.id = kNoObject; c.selected = false; sub.Add(std::move(c)); }
  std::string err;
  if (!Save3dm(sub, path, err)) { ctx.Warn(err.empty() ? "Could not write " + path : err); return; }
  def->description = "Linked: " + path;
  ctx.Print("ExportLinkedBlocks: '" + name + "' (" + std::to_string(def->objects.size()) + " object(s)) saved to " + path);
}

void RescueBlockOrphans(CommandContext& ctx) {
  std::vector<ObjectId> orphans;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    auto it = o.user_text.find("Block");
    if (it != o.user_text.end() && !ctx.Doc().FindBlock(it->second)) orphans.push_back(o.id);
  }
  if (orphans.empty()) { ctx.Print("RescueBlockOrphans: no orphaned block instances"); return; }
  ctx.Doc().BeginChange("RescueBlockOrphans");
  ctx.Doc().Ungroup(orphans);
  for (ObjectId id : orphans) if (SceneObject* o = ctx.Doc().Find(id)) { o->user_text.erase("Block"); o->user_text.erase("BlockInsert"); }
  ctx.Print("RescueBlockOrphans: " + std::to_string(orphans.size()) + " object(s) exploded from missing block definitions");
}

void BlockResetScale(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<Instance> inst = InstancesAmong(ctx, ids);
  if (inst.empty()) { ctx.Warn("Select block instances"); return; }
  ctx.Doc().BeginChange("BlockResetScale");
  int n = 0;
  for (const Instance& i : inst) {
    if (!ctx.Doc().FindBlock(i.name)) continue;
    for (ObjectId id : i.members) ctx.Doc().Remove(id);
    if (InstantiateBlock(ctx, i.name, i.insert) >= 0) ++n;
  }
  ctx.Print("BlockResetScale: " + std::to_string(n) + " instance(s) reset to the definition's scale");
}

class AddMissingBlockAttributeKeysCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    keys_ = OptionOr(TakeOptionTokens(ctx), "keys");
    if (!keys_.empty()) { Apply(ctx, ctx.Doc().SelectedIds()); return; }
    WantText("Attribute keys to add (comma separated)");
  }
  void OnText(CommandContext& ctx, const std::string& t) override { keys_ = t; Apply(ctx, ctx.Doc().SelectedIds()); }
  void Apply(CommandContext& ctx, const std::vector<ObjectId>& sel) {
    std::vector<std::string> keys;
    std::istringstream ss(keys_);
    std::string k;
    while (std::getline(ss, k, ',')) if (!k.empty()) keys.push_back(k);
    std::vector<Instance> inst = InstancesAmong(ctx, sel);
    if (keys.empty() || inst.empty()) { ctx.Warn("AddMissingBlockAttributeKeys Keys=a,b with block instances selected (or none for all)"); Finish(); return; }
    ctx.Doc().BeginChange("AddMissingBlockAttributeKeys");
    int added = 0;
    for (const Instance& i : inst) {
      for (ObjectId id : i.members) if (SceneObject* o = ctx.Doc().Find(id)) for (const std::string& key : keys) if (!o->user_text.count(key)) { o->user_text[key] = ""; ++added; }
      if (BlockDefinition* def = ctx.Doc().FindBlock(i.name)) for (SceneObject& o : def->objects) for (const std::string& key : keys) if (!o.user_text.count(key)) o.user_text[key] = "";
    }
    ctx.Print("AddMissingBlockAttributeKeys: " + std::to_string(added) + " key(s) added on " + std::to_string(inst.size()) + " instance(s)");
    Finish();
  }
  std::string keys_;
};

class SelBlockInstanceNamedCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    name_ = OptionOr(TakeOptionTokens(ctx), "name");
    if (!name_.empty()) { Apply(ctx); return; }
    ctx.Print("Blocks: " + BlockNames(ctx));
    WantText("Block name");
  }
  void OnText(CommandContext& ctx, const std::string& t) override { name_ = t; Apply(ctx); }
  void Apply(CommandContext& ctx) {
    ctx.Doc().SelectWhere([&](const SceneObject& o) { auto it = o.user_text.find("Block"); return it != o.user_text.end() && it->second == name_; });
    ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) in instances of '" + name_ + "' selected");
    Finish();
  }
  std::string name_;
};

// ---------------------------------------------------------------------------
// Picture: a planar surface tagged with the image path.
// ---------------------------------------------------------------------------

class PictureCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    path_ = OptionOr(opts, "path");
    width_ = std::atof(OptionOr(opts, "width", "0").c_str());
    height_ = std::atof(OptionOr(opts, "height", "0").c_str());
    if (path_.empty()) { WantText("Image file path"); return; }
    WantPoint("Lower-left corner of the picture");
  }
  void OnText(CommandContext&, const std::string& t) override { if (path_.empty()) { path_ = t; WantPoint("Lower-left corner of the picture"); } }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    const ON_Plane pl = ActivePlane(ctx);
    const double w = width_ > 0 ? width_ : std::max(ctx.Settings().grid_spacing * 10, 1e-6);
    const double h = height_ > 0 ? height_ : w * 0.75;
    std::vector<Point3d> grid = {p, p + pl.xaxis * w, p + pl.yaxis * h, p + pl.xaxis * w + pl.yaxis * h};
    SceneObject s = SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1));
    s.user_text["Picture"] = path_;
    s.name = std::filesystem::path(path_).filename().string();
    AddObject(ctx, std::move(s), "Picture");
    ctx.Print("Picture: " + std::filesystem::path(path_).filename().string() + " as a " + FormatNumber(w) + " x " + FormatNumber(h) + " plane (texture display is not implemented yet)");
    Finish();
  }
  std::string path_;
  double width_ = 0, height_ = 0;
};

}  // namespace

void RegisterAnnotate2Commands(CommandEngine& e) {
  const char* curves = "Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object.";
  // Dimensions and text.
  Reg(e, "DimArea", Make<MeasureDimCommand>(MeasureDimCommand::Kind::Area), CommandStatus::Partial, curves);
  Reg(e, "DimCurveLength", Make<MeasureDimCommand>(MeasureDimCommand::Kind::Length), CommandStatus::Partial, curves);
  Reg(e, "DimVolume", Make<MeasureDimCommand>(MeasureDimCommand::Kind::Volume), CommandStatus::Partial, curves);
  Reg(e, "DimOrdinate", Make<DimOrdinateCommand>(), CommandStatus::Partial, curves);
  Reg(e, "DimCreaseAngle", Make<DimCreaseAngleCommand>(), CommandStatus::Partial, "Angle between two lines or the first planar faces of two objects; no face picking on polysurfaces yet.");
  Reg(e, "DimRecenterText", OnSelection("Select dimensions to recenter text", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        const int n = EditGroups(ctx, ids, "DimRecenterText", [](GlyphSpec&) {});
        ctx.Print("DimRecenterText: " + std::to_string(n) + " annotation(s) rebuilt at their original text position");
      }), CommandStatus::Partial, "Rebuilds the text at the position it was created with.");
  Reg(e, "Centermark", Make<CentermarkCommand>());
  Reg(e, "Arrowhead", Make<ArrowheadCommand>());
  Reg(e, "RevCloud", Make<RevCloudCommand>());
  Reg(e, "TextProperties", Make<TextPropertiesCommand>("TextProperties"), CommandStatus::Partial, "Options-driven (Text=, Height=); rebuilds the text outlines of the selected annotations.");
  Reg(e, "RTextEdit", Make<TextPropertiesCommand>("RTextEdit"), CommandStatus::Partial, "Same as TextProperties.");
  Reg(e, "RLeaderEdit", Make<TextPropertiesCommand>("RLeaderEdit"), CommandStatus::Partial, "Edits the leader text; leader points stay.");
  Reg(e, "MatchAnnotation", Make<MatchAnnotationCommand>());
  Reg(e, "ScaleTextHeight", Make<ScaleTextHeightCommand>());
  Reg(e, "FindText", Make<FindTextCommand>());
  Reg(e, "SetDimensionLayer", Make<SetDimensionLayerCommand>());
  Reg(e, "AnnotationStyles", Immediate(AnnotationStylesCommand), CommandStatus::Partial, "Name= Height= Arrow= Font= creates or edits a style; bare lists them and opens Document Properties.");
  Reg(e, "DupAnnotationStyle", Immediate(DupAnnotationStyle));
  Reg(e, "ImportAnnotationStyles", Immediate(ImportAnnotationStyles));
  Reg(e, "SelAnnotationStyle", Immediate(SelAnnotationStyle));
  Reg(e, "DocumentPropertiesPage", Immediate([](CommandContext& ctx) { TakeOptionTokens(ctx); ctx.App().Panels().document_properties = true; }), CommandStatus::Partial, "Opens Document Properties (the page argument is ignored).");
  Reg(e, "Dot", Make<DotCommand>(), CommandStatus::Partial, "A point object tagged with the text (drawn as a point, not a screen-sized label).");
  Reg(e, "ConvertDots", Immediate(ConvertDots));
  Reg(e, "ConvertTextToBlockAttribute", Make<ConvertTextToBlockAttributeCommand>(), CommandStatus::Partial, "Tags the text's objects with Key=text user text.");
  Reg(e, "SelText", Immediate([](CommandContext& ctx) {
        ctx.Doc().SelectWhere([&](const SceneObject& o) { auto it = o.user_text.find("Annotation"); return it != o.user_text.end() && (it->second == "Text" || it->second == "TextObject") && ctx.Doc().IsObjectVisible(o); });
        ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " text object(s) selected");
      }));
  Reg(e, "SelDim", Immediate([](CommandContext& ctx) {
        ctx.Doc().SelectWhere([&](const SceneObject& o) { auto it = o.user_text.find("Annotation"); return it != o.user_text.end() && (it->second.compare(0, 3, "Dim") == 0 || it->second == "Leader") && ctx.Doc().IsObjectVisible(o); });
        ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " dimension object(s) selected");
      }));
  // Hatch extras.
  Reg(e, "HatchBase", Make<HatchBaseCommand>());
  Reg(e, "HatchScale", Make<HatchScaleCommand>());
  Reg(e, "SelHatch", Immediate([](CommandContext& ctx) {
        ctx.Doc().SelectWhere([&](const SceneObject& o) { return o.user_text.count("Hatch") > 0 && ctx.Doc().IsObjectVisible(o); });
        ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " hatch object(s) selected");
      }));
  // Linetypes.
  Reg(e, "Linetypes", Immediate(ListLinetypes));
  Reg(e, "Linetype", Immediate(ListLinetypes));
  Reg(e, "SetLinetype", Make<SetLinetypeCommand>());
  Reg(e, "SetLayerLinetype", Make<SetLayerLinetypeCommand>());
  Reg(e, "SetLinetypeScale", Make<SetLinetypeScaleCommand>());
  Reg(e, "LinetypeDisplay", Immediate(LinetypeDisplay));
  Reg(e, "SetCustomLinetype", Make<SetCustomLinetypeCommand>());
  Reg(e, "ExtractLineTypeSegments", Make<ExtractLineTypeSegmentsCommand>());
  Reg(e, "SelLinetype", Make<SelLinetypeCommand>());
  // Blocks.
  Reg(e, "BlockEdit", Make<BlockEditCommand>(), CommandStatus::Partial, "Places an editable copy of the definition at the instance; running BlockEdit again redefines the block from it.");
  Reg(e, "AddObjectsToBlock", Make<AddObjectsToBlockCommand>());
  Reg(e, "ReplaceBlock", Make<ReplaceBlockCommand>());
  Reg(e, "CreateUniqueBlock", Make<CreateUniqueBlockCommand>());
  Reg(e, "ExportLinkedBlocks", Immediate(ExportLinkedBlocks), CommandStatus::Partial, "Saves the definition's objects to a .3dm (Name=, Path=); the block stays embedded.");
  Reg(e, "RescueBlockOrphans", Immediate(RescueBlockOrphans));
  Reg(e, "BlockResetScale", OnSelection("Select block instances", BlockResetScale), CommandStatus::Partial, "Re-inserts the instance at its insertion point, dropping any scaling or rotation.");
  Reg(e, "AddMissingBlockAttributeKeys", Make<AddMissingBlockAttributeKeysCommand>(), CommandStatus::Partial, "Adds empty user-text keys (Keys=a,b) to instances and their definition.");
  Reg(e, "SelBlockInstanceNamed", Make<SelBlockInstanceNamedCommand>());
  // Picture.
  Reg(e, "Picture", Make<PictureCommand>(), CommandStatus::Partial, "Creates the picture plane tagged with the image path; the image texture is not displayed yet.");
}

}  // namespace dino8::app
