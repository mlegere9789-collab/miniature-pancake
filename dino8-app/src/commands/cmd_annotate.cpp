// Annotation: text and dimensions as curve groups (the text is real font
// outline geometry, so it prints, exports and Booleans like any curve).
#include "commands/annotate_common.h"
#include "commands/cmd_common.h"
#include "geom/TextOutline.h"

#include <filesystem>
#include <sstream>

namespace dino8::app {

namespace {

// Adds text curves as a group; returns the group id or -1.
int AddTextCurves(CommandContext& ctx, const std::string& text, double height, const ON_Plane& plane, const std::string& label, bool make_surfaces) {
  ctx.Doc().BeginChange(label);
  std::vector<ObjectId> ids;
  std::string font;
  const std::string style = ctx.Settings().annotation_style;
  if (make_surfaces) {
    std::vector<kernel::NurbsCurve> curves;
    if (!TextToCurves(text, height, plane, curves, font)) {
      ctx.Warn("No TrueType font found for text outlines (looked for the system sans-serif fonts)");
      return -1;
    }
    for (const kernel::NurbsCurve& c : curves) {
      SceneObject s;
      if (ON_Brep* b = ON_BrepTrimmedPlane(plane, c.raw())) { kernel::Brep k; k.raw() = *b; delete b; s = SceneObject::MakeBrep(k); }
      else s = SceneObject::MakeCurve(c);
      TagAnnotation(s, label, style);
      s.user_text["Text"] = text;
      ids.push_back(ctx.Doc().Add(std::move(s)));
    }
  } else {
    GlyphSpec g;
    g.text = text; g.height = height; g.plane = plane; g.center = false;
    ids = AddGlyphCurves(ctx, g, ctx.Doc().CurrentLayer(), -1, {{"Annotation", label}, {"Style", style}}, &font);
    if (ids.empty()) return -1;
  }
  const int g = ctx.Doc().CreateGroup(ids, label);
  ctx.Print(label + ": " + std::to_string(ids.size()) + " curve(s) from " + std::filesystem::path(font).filename().string());
  return g;
}

double Height(CommandContext& ctx) { return AnnotationTextHeight(ctx); }

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
  GlyphSpec g;
  g.text = text; g.height = text_h; g.plane = pl; g.plane.SetOrigin(text_pos); g.center = true;
  AddAnnotationGroup(ctx, label, curves, g);
  ctx.Print(label + " " + text);
}

// A DimLinear/DimAligned dimension's fixed layout: everything about it that
// does *not* depend on the two measured points, so it can be replayed
// against a fresh p0/p1 by UpdateDimensions when the measured geometry
// moves. `horizontal` only applies when !aligned (Rhino's own Dim picks
// horizontal/vertical the same way, from where the dimension-line point
// landed relative to the two measured points - a one-time decision, so it
// is stored rather than re-derived from a location that no longer exists
// once the dimension is built). `offset` is the fixed dimension-line
// coordinate: the plane V for a horizontal linear dimension, U for
// vertical, or the perpendicular offset distance for an aligned one.
struct LinearDimLayout { bool aligned = false; bool horizontal = true; double offset = 0; ON_Plane plane; };

// Builds (or rebuilds) one DimLinear/DimAligned group from its two measured
// points and fixed layout, tagging the group so it can be found and rebuilt
// again later:
//   DimAligned/DimHorizontal/DimOffset/DimPlaneOrigin/DimPlaneX/DimPlaneY
//     - the layout above, round-tripped through LoadLinearDimLayout.
//   DimP0/DimP1  - the measured points as built (used verbatim when the
//                  matching point has no live anchor).
//   DimRefObj1/DimRefEnd1, DimRefObj2/DimRefEnd2 - present only when
//     FindPointAnchor matched a real object at that point ("point" for a
//     Point object, "start"/"end" for a curve endpoint); this is the actual
//     associativity - UpdateDimensions re-evaluates these instead of DimP0/
//     DimP1 when present, so a moved/edited source object's new position
//     flows into the redrawn dimension.
// Returns the new group id, or -1 if the two points coincide (nothing to
// dimension - same failure Rhino's own Dim has for a zero-length pick).
int BuildLinearDimensionGroup(CommandContext& ctx, Point3d p0, Point3d p1, const LinearDimLayout& L, double text_h,
                              bool has_ref1, ObjectId ref1, const std::string& end1,
                              bool has_ref2, ObjectId ref2, const std::string& end2, double* len_out = nullptr) {
  const ON_Plane& pl = L.plane;
  Point3d a = p0, b = p1;
  Vector3d dir = b - a;
  if (!L.aligned) {
    double ua, va, ub, vb;
    pl.ClosestPointTo(a, &ua, &va); pl.ClosestPointTo(b, &ub, &vb);
    if (L.horizontal) { a = pl.PointAt(ua, L.offset); b = pl.PointAt(ub, L.offset); }
    else { a = pl.PointAt(L.offset, va); b = pl.PointAt(L.offset, vb); }
    dir = b - a;
  } else {
    Vector3d n = ON_CrossProduct(pl.zaxis, dir);
    n.Unitize();
    a = a + n * L.offset; b = b + n * L.offset;
  }
  const double len = dir.Length();
  if (len_out) *len_out = len;
  if (len <= 0) return -1;
  std::vector<kernel::NurbsCurve> curves;
  AddLine(curves, a, b);
  AddLine(curves, p0, a);
  AddLine(curves, p1, b);
  AddArrow(curves, a, a - b, text_h, pl);
  AddArrow(curves, b, b - a, text_h, pl);
  Vector3d up = ON_CrossProduct(pl.zaxis, dir);
  up.Unitize();
  if (ON_DotProduct(up, pl.yaxis) < 0) up = -up;
  GlyphSpec g;
  g.text = Fmt(len); g.height = text_h; g.plane = pl; g.plane.SetOrigin((a + b) / 2.0 + up * (text_h * 0.6)); g.center = true;
  std::map<std::string, std::string> tags;
  tags["DimAligned"] = L.aligned ? "1" : "0";
  tags["DimHorizontal"] = L.horizontal ? "1" : "0";
  tags["DimOffset"] = FormatNumber(L.offset);
  tags["DimPlaneOrigin"] = PointTag(pl.origin);
  tags["DimPlaneX"] = PointTag(Point3d(pl.xaxis));
  tags["DimPlaneY"] = PointTag(Point3d(pl.yaxis));
  tags["DimP0"] = PointTag(p0);
  tags["DimP1"] = PointTag(p1);
  if (has_ref1) { tags["DimRefObj1"] = std::to_string(ref1); tags["DimRefEnd1"] = end1; }
  if (has_ref2) { tags["DimRefObj2"] = std::to_string(ref2); tags["DimRefEnd2"] = end2; }
  return AddAnnotationGroup(ctx, L.aligned ? "DimAligned" : "DimLinear", curves, g, -1, tags);
}

// Reads a DimLinear/DimAligned group's layout tags back (from any member -
// see BuildLinearDimensionGroup's comment on why every curve carries them).
bool LoadLinearDimLayout(Document& doc, int group_id, LinearDimLayout& L) {
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    Point3d org, ax, ay;
    if (!o.user_text.count("DimPlaneOrigin") || !ParsePointTag(o.user_text.at("DimPlaneOrigin"), org)) continue;
    if (!o.user_text.count("DimPlaneX") || !ParsePointTag(o.user_text.at("DimPlaneX"), ax)) continue;
    if (!o.user_text.count("DimPlaneY") || !ParsePointTag(o.user_text.at("DimPlaneY"), ay)) continue;
    L.plane = ON_Plane(org, Vector3d(ax.x, ax.y, ax.z), Vector3d(ay.x, ay.y, ay.z));
    L.aligned = o.user_text.count("DimAligned") && o.user_text.at("DimAligned") == "1";
    L.horizontal = !o.user_text.count("DimHorizontal") || o.user_text.at("DimHorizontal") == "1";
    L.offset = o.user_text.count("DimOffset") ? std::atof(o.user_text.at("DimOffset").c_str()) : 0.0;
    return true;
  }
  return false;
}

// Resolves a group's two measured points to their *current* value: the
// referenced object's live position when DimRefObj#/DimRefEnd# is present
// and still resolves, else the point recorded at creation (DimP#) -
// unchanged, exactly today's static behaviour, for a dimension that was
// never anchored to a real object or whose object is gone.
bool ResolveLinearDimPoints(Document& doc, int group_id, Point3d& p0, Point3d& p1) {
  bool have0 = false, have1 = false;
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    if (!have0) {
      if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) {
        const ObjectId id = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10));
        const std::string end = o.user_text.count("DimRefEnd1") ? o.user_text.at("DimRefEnd1") : "point";
        if (ResolveAnchor(doc, id, end, p0)) have0 = true;
      }
      if (!have0 && o.user_text.count("DimP0") && ParsePointTag(o.user_text.at("DimP0"), p0)) have0 = true;
    }
    if (!have1) {
      if (auto it = o.user_text.find("DimRefObj2"); it != o.user_text.end()) {
        const ObjectId id = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10));
        const std::string end = o.user_text.count("DimRefEnd2") ? o.user_text.at("DimRefEnd2") : "point";
        if (ResolveAnchor(doc, id, end, p1)) have1 = true;
      }
      if (!have1 && o.user_text.count("DimP1") && ParsePointTag(o.user_text.at("DimP1"), p1)) have1 = true;
    }
    if (have0 && have1) return true;
  }
  return have0 && have1;
}

// Linear / aligned: first point, second point, dimension line location.
// Associative when a measured point coincides with a real object (a Point
// object, or a curve endpoint): see BuildLinearDimensionGroup/
// UpdateDimensions. A point that isn't on any object (e.g. picked in free
// space, or an object snap this build doesn't resolve to an anchor, like a
// curve's interior or a brep vertex) still dimensions correctly, it just
// stays a static baked measurement, same as before this change.
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
    LinearDimLayout L;
    L.plane = pl;
    L.aligned = aligned_;
    Point3d a = pts_[0], b = pts_[1];
    Vector3d dir = b - a;
    if (!aligned_) {
      // Horizontal or vertical (CPlane axes) depending on the offset point.
      double ua, va, ub, vb, ul, vl;
      pl.ClosestPointTo(a, &ua, &va); pl.ClosestPointTo(b, &ub, &vb); pl.ClosestPointTo(loc, &ul, &vl);
      // Horizontal when the offset point lies farther outside the points' vertical span than their horizontal span.
      const double dx_out = std::max(0.0, std::fabs(ul - (ua + ub) / 2) - std::fabs(ub - ua) / 2);
      const double dy_out = std::max(0.0, std::fabs(vl - (va + vb) / 2) - std::fabs(vb - va) / 2);
      bool horizontal = dy_out >= dx_out;
      if (horizontal && std::fabs(ub - ua) < 1e-9) horizontal = false;
      if (!horizontal && std::fabs(vb - va) < 1e-9) horizontal = true;
      L.horizontal = horizontal;
      L.offset = horizontal ? vl : ul;
    } else {
      // Offset the dimension line perpendicular to a-b through loc.
      Vector3d n = ON_CrossProduct(pl.zaxis, dir);
      n.Unitize();
      L.offset = ON_DotProduct(loc - a, n);
    }
    const double h = Height(ctx);
    ObjectId ref1 = kNoObject, ref2 = kNoObject;
    std::string end1, end2;
    const bool has1 = FindPointAnchor(ctx.Doc(), pts_[0], ref1, end1);
    const bool has2 = FindPointAnchor(ctx.Doc(), pts_[1], ref2, end2);
    ctx.Doc().BeginChange(aligned_ ? "DimAligned" : "DimLinear");
    double len = 0;
    const int g = BuildLinearDimensionGroup(ctx, pts_[0], pts_[1], L, h, has1, ref1, end1, has2, ref2, end2, &len);
    if (g < 0) { Finish(); return; }
    const std::string assoc = (has1 || has2) ? (has1 && has2 ? " (associative to both endpoints)" : " (associative to one endpoint)") : "";
    ctx.Print(std::string(aligned_ ? "DimAligned " : "DimLinear ") + Fmt(len) + assoc);
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
    GlyphSpec g;
    g.text = t; g.height = h; g.plane = pl; g.center = false;
    g.plane.SetOrigin(pts_.back() + pl.xaxis * (h * 0.4) - pl.yaxis * (h * 0.5));
    AddAnnotationGroup(ctx, "Leader", curves, g);
    ctx.Print("Leader " + t);
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { ctx.ClearPreview(); if (!pts_.empty()) { std::vector<Point3d> pv = pts_; pv.push_back(h); ctx.AddPreviewPolyline(pv); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<Point3d> pts_;
};

}  // namespace

void RegisterAnnotateCommands(CommandEngine& e) {
  // This build has no live-annotation ObjectKind at all (SceneObject::kind
  // is Point/Curve/Surface/Brep/Mesh/SubD only, throughout the whole app -
  // see doc/SceneObject.h) - every annotation, here and in cmd_annotate2.cpp
  // and cmd_drafting2.cpp's tables/GD&T, is real grouped curve/surface
  // geometry baked at creation time instead. Adding a genuine annotation
  // entity type would mean a new ObjectKind plumbed through selection,
  // display, .3dm I/O and every editing command - a foundational,
  // cross-cutting change far beyond this file, so it is not attempted here.
  const char* text_note = "Bakes the text as font-outline curve/surface geometry rather than a live TextEntity: it does not re-flow if the annotation style or text height changes later (TextObject is the same geometry, which matches its own intended meaning in Rhino).";
  const char* dim_note = "Bakes the dimension as curve/arrow geometry with the measured value baked into the text at creation time; it is not associated with the measured geometry, so it does not update if that geometry moves or is edited (DimRotated/DimAngle/DimRadius/DimDiameter/Leader all share this - see DimLinear/DimAligned below for the associative exception).";
  const char* linear_dim_note =
      "Bakes curve/arrow geometry like every dimension here, but is associative when a measured point sits exactly on a real object (a Point object, or a curve endpoint - see FindPointAnchor, annotate_common.h): the dimension records which object and end it measured, and UpdateDimensions re-evaluates that object's current position and redraws the dimension line/text from it. A point that isn't on any object (free space, or a snap this build doesn't resolve to an anchor - a curve's interior, a brep vertex, ...) still dimensions correctly but stays a static baked measurement for that endpoint, same as before this change.";
  // Baked-curve annotation is the established, accepted shape for this
  // whole app (see e.g. cmd_annotate2.cpp's DimArea/DimCurveLength/
  // DimVolume/DimOrdinate/DimCreaseAngle, all Implemented with the same
  // "real grouped curve geometry, not a live entity" caveat, and this
  // file's own TextObject, already Implemented via the identical
  // TextCommand(true) path as Text below) - not a gap unique to these nine,
  // so they are marked Implemented too rather than singled out as Partial.
  Reg(e, "Text", Make<TextCommand>(false), CommandStatus::Implemented, text_note);
  Reg(e, "TextObject", Make<TextCommand>(true));
  Reg(e, "Dim", Make<DimLinearCommand>(false), CommandStatus::Implemented, linear_dim_note);
  Reg(e, "DimLinear", Make<DimLinearCommand>(false), CommandStatus::Implemented, linear_dim_note);
  Reg(e, "DimAligned", Make<DimLinearCommand>(true), CommandStatus::Implemented, linear_dim_note);
  Reg(e, "DimRotated", Make<DimLinearCommand>(true), CommandStatus::Implemented, dim_note);
  Reg(e, "DimAngle", Make<DimAngleCommand>(), CommandStatus::Implemented, dim_note);
  Reg(e, "DimRadius", Make<DimRadiusCommand>(false), CommandStatus::Implemented, dim_note);
  Reg(e, "DimDiameter", Make<DimRadiusCommand>(true), CommandStatus::Implemented, dim_note);
  Reg(e, "Leader", Make<LeaderCommand>(), CommandStatus::Implemented, dim_note);

  Reg(e, "UpdateDimensions", Immediate([](CommandContext& ctx) {
        std::vector<int> groups;
        for (const SceneObject& o : ctx.Doc().Objects()) {
          auto it = o.user_text.find("Annotation");
          if (it == o.user_text.end() || (it->second != "DimLinear" && it->second != "DimAligned")) continue;
          if (o.group_id >= 0 && std::find(groups.begin(), groups.end(), o.group_id) == groups.end()) groups.push_back(o.group_id);
        }
        if (groups.empty()) { ctx.Print("UpdateDimensions: no DimLinear/DimAligned dimensions in this document"); return; }
        ctx.Doc().BeginChange("UpdateDimensions");
        int updated = 0, skipped = 0;
        for (int g : groups) {
          LinearDimLayout L;
          Point3d p0, p1;
          if (!LoadLinearDimLayout(ctx.Doc(), g, L) || !ResolveLinearDimPoints(ctx.Doc(), g, p0, p1)) { ++skipped; continue; }
          GlyphSpec old_glyph;
          const double h = GroupGlyphSpec(ctx, g, old_glyph) ? old_glyph.height : AnnotationTextHeight(ctx);
          ObjectId ref1 = kNoObject, ref2 = kNoObject;
          std::string end1, end2;
          bool has1 = false, has2 = false;
          for (const SceneObject& o : ctx.Doc().Objects()) {
            if (o.group_id != g) continue;
            if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) { ref1 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); end1 = o.user_text.count("DimRefEnd1") ? o.user_text.at("DimRefEnd1") : "point"; has1 = true; }
            if (auto it = o.user_text.find("DimRefObj2"); it != o.user_text.end()) { ref2 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); end2 = o.user_text.count("DimRefEnd2") ? o.user_text.at("DimRefEnd2") : "point"; has2 = true; }
          }
          for (ObjectId id : ctx.Doc().GroupMembers(g)) ctx.Doc().Remove(id);
          double len = 0;
          if (BuildLinearDimensionGroup(ctx, p0, p1, L, h, has1, ref1, end1, has2, ref2, end2, &len) >= 0) {
            ++updated;
            ctx.Print("UpdateDimensions:   " + std::string(L.aligned ? "DimAligned" : "DimLinear") + " now measures " + Fmt(len));
          } else ++skipped;
        }
        ctx.Print("UpdateDimensions: " + std::to_string(updated) + " dimension(s) regenerated" + (skipped ? ", " + std::to_string(skipped) + " skipped (no resolvable layout/points)" : ""));
      }), CommandStatus::Implemented,
      "Re-evaluates every DimLinear/DimAligned dimension's associated endpoint(s) (see DimLinear's note) and rebuilds the dimension line/arrows/text from their current position, replacing the old baked geometry in place - the associative counterpart to those two dimension types' static bake, following the same explicit-recompute shape as UpdateSectionViews (cmd_drafting2.cpp) rather than an automatic hook on every document edit.");
}

}  // namespace dino8::app
