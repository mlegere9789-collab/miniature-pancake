// Annotation: text and dimensions as curve groups (the text is real font
// outline geometry, so it prints, exports and Booleans like any curve).
#include "commands/cmd_common.h"
#include "geom/TextOutline.h"

#include <filesystem>
#include <sstream>

namespace dino8::app {

namespace {

// Adds text curves as a group; returns the group id or -1.
int AddTextCurves(CommandContext& ctx, const std::string& text, double height, const ON_Plane& plane, const std::string& label, bool make_surfaces) {
  std::vector<kernel::NurbsCurve> curves;
  std::string font;
  if (!TextToCurves(text, height, plane, curves, font)) {
    ctx.Warn("No TrueType font found for text outlines (looked for the system sans-serif fonts)");
    return -1;
  }
  ctx.Doc().BeginChange(label);
  std::vector<ObjectId> ids;
  for (const kernel::NurbsCurve& c : curves) {
    if (make_surfaces) {
      if (ON_Brep* b = ON_BrepTrimmedPlane(plane, c.raw())) { kernel::Brep k; k.raw() = *b; delete b; ids.push_back(ctx.Doc().Add(SceneObject::MakeBrep(k))); continue; }
    }
    ids.push_back(ctx.Doc().Add(SceneObject::MakeCurve(c)));
  }
  const int g = ctx.Doc().CreateGroup(ids, label);
  ctx.Print(label + ": " + std::to_string(ids.size()) + " curve(s) from " + std::filesystem::path(font).filename().string());
  return g;
}

double Height(CommandContext& ctx) { return std::max(ctx.Settings().grid_spacing * 2.0, 1e-6); }

class TextCommand : public Command {
 public:
  explicit TextCommand(bool object) : object_(object) {}
  void Begin(CommandContext& ctx) override {
    height_ = Height(ctx);
    WantText("Text to create");
    options = {{"Height", FormatNumber(height_), {}, true, false}, {"Output", "Curves", {"Curves", "Surfaces"}, false, false}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Height") { double h = std::atof(v.c_str()); if (h > 0) height_ = h; options[0].value = FormatNumber(height_); }
    if (n == "Output") { surfaces_ = !surfaces_; options[1].value = surfaces_ ? "Surfaces" : "Curves"; }
  }
  void OnText(CommandContext&, const std::string& t) override {
    if (text_.empty()) { text_ = t; WantPoint("Start point of text"); }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(p);
    AddTextCurves(ctx, text_, height_, pl, object_ ? "TextObject" : "Text", surfaces_);
    Finish();
  }
  bool object_;
  bool surfaces_ = false;
  double height_ = 1;
  std::string text_;
};

// Shared dimension drawing helpers.
void AddLine(std::vector<kernel::NurbsCurve>& out, Point3d a, Point3d b) { out.push_back(PolylineCurve({a, b})); }
void AddArrow(std::vector<kernel::NurbsCurve>& out, Point3d tip, Vector3d dir, double size, const ON_Plane& pl) {
  dir.Unitize();
  Vector3d side = ON_CrossProduct(pl.zaxis, dir);
  side.Unitize();
  const Point3d a = tip - dir * size + side * (size * 0.3), b = tip - dir * size - side * (size * 0.3);
  out.push_back(PolylineCurve({a, tip, b, a}));
}
std::string Fmt(double v) { return FormatNumber(v); }

void AddDimension(CommandContext& ctx, const std::string& label, std::vector<kernel::NurbsCurve>& curves, const std::string& text, Point3d text_pos, const ON_Plane& pl, double text_h) {
  ctx.Doc().BeginChange(label);
  std::vector<ObjectId> ids;
  for (const kernel::NurbsCurve& c : curves) ids.push_back(ctx.Doc().Add(SceneObject::MakeCurve(c)));
  std::vector<kernel::NurbsCurve> glyphs;
  std::string font;
  double width = 0;
  ON_Plane tp = pl;
  tp.SetOrigin(text_pos);
  if (TextToCurves(text, text_h, tp, glyphs, font, &width)) {
    // Centre the text on text_pos.
    const ON_Xform shift = ON_Xform::TranslationTransformation(-pl.xaxis * (width / 2));
    for (kernel::NurbsCurve& g : glyphs) { g.raw().Transform(shift); ids.push_back(ctx.Doc().Add(SceneObject::MakeCurve(g))); }
  }
  ctx.Doc().CreateGroup(ids, label);
  ctx.Print(label + " " + text);
}

// Linear / aligned: first point, second point, dimension line location.
class DimLinearCommand : public Command {
 public:
  explicit DimLinearCommand(bool aligned) : aligned_(aligned) {}
  void Begin(CommandContext&) override { WantPoint("First dimension point"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) { WantPoint("Second dimension point"); return; }
    if (pts_.size() == 2) { WantPoint("Dimension line location"); return; }
    Build(ctx, p);
  }
  void Build(CommandContext& ctx, Point3d loc) {
    ctx.ClearPreview();
    const ON_Plane pl = ActivePlane(ctx);
    Point3d a = pts_[0], b = pts_[1];
    Vector3d dir = b - a;
    if (!aligned_) {
      // Horizontal or vertical (CPlane axes) depending on the offset point.
      double ua, va, ub, vb, ul, vl;
      pl.ClosestPointTo(a, &ua, &va); pl.ClosestPointTo(b, &ub, &vb); pl.ClosestPointTo(loc, &ul, &vl);
      const bool horizontal = std::fabs(vl - (va + vb) / 2) > std::fabs(ul - (ua + ub) / 2);
      if (horizontal) { a = pl.PointAt(ua, vl); b = pl.PointAt(ub, vl); }
      else { a = pl.PointAt(ul, va); b = pl.PointAt(ul, vb); }
      dir = b - a;
    } else {
      // Offset the dimension line perpendicular to a-b through loc.
      Vector3d n = ON_CrossProduct(pl.zaxis, dir);
      n.Unitize();
      const double off = ON_DotProduct(loc - a, n);
      a = a + n * off; b = b + n * off;
    }
    const double len = dir.Length();
    if (len <= 0) { Finish(); return; }
    const double h = Height(ctx);
    std::vector<kernel::NurbsCurve> curves;
    AddLine(curves, a, b);
    AddLine(curves, pts_[0], a);
    AddLine(curves, pts_[1], b);
    AddArrow(curves, a, a - b, h, pl);
    AddArrow(curves, b, b - a, h, pl);
    Vector3d up = ON_CrossProduct(pl.zaxis, dir);
    up.Unitize();
    if (ON_DotProduct(up, pl.yaxis) < 0) up = -up;
    AddDimension(ctx, aligned_ ? "DimAligned" : "DimLinear", curves, Fmt(len), (a + b) / 2.0 + up * (h * 0.6), pl, h);
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    if (pts_.size() == 1) ctx.AddPreviewLine(pts_[0], h);
    if (pts_.size() == 2) { ctx.AddPreviewLine(pts_[0], pts_[1]); ctx.AddPreviewLine((pts_[0] + pts_[1]) / 2.0, h); }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  bool aligned_;
  std::vector<Point3d> pts_;
};

class DimAngleCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Vertex of angle"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) { WantPoint("First direction point"); return; }
    if (pts_.size() == 2) { WantPoint("Second direction point"); return; }
    ctx.ClearPreview();
    const ON_Plane pl = ActivePlane(ctx);
    Vector3d va = pts_[1] - pts_[0], vb = pts_[2] - pts_[0];
    const double r = std::min(va.Length(), vb.Length()) * 0.7;
    if (r <= 0) { Finish(); return; }
    va.Unitize(); vb.Unitize();
    double a0 = std::atan2(ON_DotProduct(va, pl.yaxis), ON_DotProduct(va, pl.xaxis));
    double a1 = std::atan2(ON_DotProduct(vb, pl.yaxis), ON_DotProduct(vb, pl.xaxis));
    if (a1 < a0) std::swap(a0, a1);
    if (a1 - a0 > ON_PI) { std::swap(a0, a1); a1 += 2 * ON_PI; }
    ON_Plane cp = pl; cp.SetOrigin(pts_[0]);
    ON_Arc arc(ON_Circle(cp, r), ON_Interval(a0, a1));
    std::vector<kernel::NurbsCurve> curves;
    ON_ArcCurve ac(arc);
    kernel::NurbsCurve k;
    if (CurveFromON(ac, k)) curves.push_back(k);
    AddLine(curves, pts_[0], pts_[0] + va * (r * 1.1));
    AddLine(curves, pts_[0], pts_[0] + vb * (r * 1.1));
    const double h = Height(ctx);
    const double mid = (a0 + a1) / 2;
    const Point3d tp = cp.PointAt(std::cos(mid) * (r + h), std::sin(mid) * (r + h));
    AddDimension(ctx, "DimAngle", curves, Fmt((a1 - a0) * 180.0 / ON_PI) + " deg", tp, pl, h);
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { ctx.ClearPreview(); if (!pts_.empty()) ctx.AddPreviewLine(pts_[0], h); if (pts_.size() > 1) ctx.AddPreviewLine(pts_[0], pts_[1]); }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<Point3d> pts_;
};

class DimRadiusCommand : public Command {
 public:
  explicit DimRadiusCommand(bool diameter) : diameter_(diameter) {}
  void Begin(CommandContext&) override { WantObjects("Select an arc or circle"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      ON_Arc arc;
      if (o && o->kind == ObjectKind::Curve && o->curve->raw().IsArc(nullptr, &arc)) { arc_ = arc; have_ = true; break; }
    }
    if (!have_) { ctx.Warn("Select an arc or circle"); Finish(); return; }
    WantPoint("Dimension location");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    const ON_Plane pl = ActivePlane(ctx);
    Vector3d d = p - arc_.Center();
    if (!d.Unitize()) d = pl.xaxis;
    const Point3d on = arc_.Center() + d * arc_.Radius();
    std::vector<kernel::NurbsCurve> curves;
    const double h = Height(ctx);
    if (diameter_) { AddLine(curves, arc_.Center() - d * arc_.Radius(), p); AddArrow(curves, arc_.Center() - d * arc_.Radius(), -d, h, pl); }
    else AddLine(curves, arc_.Center(), p);
    AddArrow(curves, on, d, h, pl);
    AddDimension(ctx, diameter_ ? "DimDiameter" : "DimRadius", curves, std::string(diameter_ ? "D " : "R ") + Fmt(diameter_ ? arc_.Radius() * 2 : arc_.Radius()), p + d * h, pl, h);
    Finish();
  }
  bool diameter_;
  bool have_ = false;
  ON_Arc arc_;
};

class LeaderCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Start of leader (arrowhead)"); }
  void OnPoint(CommandContext& ctx, Point3d p) override { pts_.push_back(p); ctx.SetLastPoint(p); WantPoint("Next point. Press Enter for text"); }
  void OnEnter(CommandContext&) override { if (pts_.size() >= 2) WantText("Leader text"); else Finish(); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    ctx.ClearPreview();
    const ON_Plane pl = ActivePlane(ctx);
    const double h = Height(ctx);
    std::vector<kernel::NurbsCurve> curves;
    curves.push_back(PolylineCurve(pts_));
    AddArrow(curves, pts_[0], pts_[0] - pts_[1], h, pl);
    Vector3d dir = pts_.back() - pts_[pts_.size() - 2];
    dir.Unitize();
    ctx.Doc().BeginChange("Leader");
    std::vector<ObjectId> ids;
    for (const kernel::NurbsCurve& c : curves) ids.push_back(ctx.Doc().Add(SceneObject::MakeCurve(c)));
    ON_Plane tp = pl;
    tp.SetOrigin(pts_.back() + pl.xaxis * (h * 0.4) - pl.yaxis * (h * 0.5));
    std::vector<kernel::NurbsCurve> glyphs;
    std::string font;
    if (TextToCurves(t, h, tp, glyphs, font)) for (const kernel::NurbsCurve& g : glyphs) ids.push_back(ctx.Doc().Add(SceneObject::MakeCurve(g)));
    ctx.Doc().CreateGroup(ids, "Leader");
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { ctx.ClearPreview(); if (!pts_.empty()) { std::vector<Point3d> pv = pts_; pv.push_back(h); ctx.AddPreviewPolyline(pv); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<Point3d> pts_;
};

}  // namespace

void RegisterAnnotateCommands(CommandEngine& e) {
  const char* note = "Creates grouped curve geometry (text outlines from the system font) rather than a live annotation object.";
  Reg(e, "Text", Make<TextCommand>(false), CommandStatus::Partial, note);
  Reg(e, "TextObject", Make<TextCommand>(true));
  Reg(e, "Dim", Make<DimLinearCommand>(false), CommandStatus::Partial, note);
  Reg(e, "DimLinear", Make<DimLinearCommand>(false), CommandStatus::Partial, note);
  Reg(e, "DimAligned", Make<DimLinearCommand>(true), CommandStatus::Partial, note);
  Reg(e, "DimRotated", Make<DimLinearCommand>(true), CommandStatus::Partial, note);
  Reg(e, "DimAngle", Make<DimAngleCommand>(), CommandStatus::Partial, note);
  Reg(e, "DimRadius", Make<DimRadiusCommand>(false), CommandStatus::Partial, note);
  Reg(e, "DimDiameter", Make<DimRadiusCommand>(true), CommandStatus::Partial, note);
  Reg(e, "Leader", Make<LeaderCommand>(), CommandStatus::Partial, note);
}

}  // namespace dino8::app
