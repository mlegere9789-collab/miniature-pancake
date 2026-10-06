// Annotation: text and dimensions as curve groups (the text is real font
// outline geometry, so it prints, exports and Booleans like any curve).
#include "commands/DimGeometry.h"
#include "commands/annotate_common.h"
#include "commands/cmd_common.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/surface_intersect.h"
#include "geom/TextOutline.h"

#include <filesystem>
#include <sstream>

namespace dino8::app {

namespace {

// Signed area of a 2D polygon (shoelace formula) - this file's own local
// copy of the same algorithm boolean.cpp/surface.cpp/brep.cpp each already
// keep a file-local copy of (see e.g. boolean.cpp's own PointInPolygon2D
// comment), used here only to pick the SMALLEST of several candidate
// containing contours (the immediate parent, not a further ancestor), not
// for any fill-rule decision.
double PolygonAreaAbs(const std::vector<ON_2dPoint>& poly) {
  double a = 0;
  for (size_t i = 0; i + 1 < poly.size(); ++i) a += poly[i].x * poly[i + 1].y - poly[i + 1].x * poly[i].y;
  if (poly.size() > 1) a += poly.back().x * poly.front().y - poly.front().x * poly.back().y;
  return std::fabs(a) * 0.5;
}

// Samples a closed glyph-contour curve into `plane`'s own local (u, v)
// polygon, for the containment tests below.
std::vector<ON_2dPoint> ContourPolygon2d(const kernel::NurbsCurve& c, const ON_Plane& plane, int samples = 48) {
  std::vector<ON_2dPoint> poly;
  double t0 = 0, t1 = 0;
  c.raw().GetDomain(&t0, &t1);
  for (int i = 0; i < samples; ++i) {
    const double t = t0 + (t1 - t0) * (static_cast<double>(i) / samples);
    double u, v;
    plane.ClosestPointTo(c.raw().PointAt(t), &u, &v);
    poly.emplace_back(u, v);
  }
  return poly;
}

// Groups `contours` (every glyph's own outline, flattened across the whole
// text string by TextToCurves - one closed curve per TrueType contour, no
// outer/counter distinction) into standalone solid letters: for a glyph
// like "O"/"A"/"B" whose outline is more than one contour, the inner
// contour(s) are its own counters (the hole in an "O", both counters in a
// "B"), not separate glyphs. Classified purely by 2D containment (the same
// even-odd nesting rule a font's own contours already follow - no reliance
// on winding direction, which this codebase's own font decomposer doesn't
// normalize): a contour's `depth` is how many OTHER contours' own polygons
// contain it; even depth is solid material (a glyph's own outer boundary,
// or a rare island nested inside a hole inside an outer - out of scope,
// see below), odd depth is a hole. Each hole's immediate parent is the
// smallest even-depth polygon that contains it (not merely a depth-0 one),
// so this also handles a hole nested one level inside another (via its own
// `depth-1` match), not just a flat one-level "letter + its holes" case.
//
// Builds one Brep per depth-0 contour via kernel::Brep::Extrude() (no
// holes) or kernel::ExtrudeProfileWithHoles() (one or more). A contour at
// depth 2 or deeper (an island nested inside a hole inside an outer - not
// produced by any Latin/ASCII glyph in the font files this kernel
// searches for, see TextOutline.cpp's own CandidateFontFiles()) is simply
// not built as its own solid (out of scope, disclosed rather than
// mishandled); a hole whose own immediate parent can't be found (the same
// never-observed case) is dropped from its would-be parent's hole list
// rather than left to corrupt the boolean composition.
std::vector<kernel::Brep> BuildSolidLetters(const std::vector<kernel::NurbsCurve>& contours, const ON_Plane& plane,
                                            double thickness, CommandContext& ctx) {
  std::vector<kernel::Brep> result;
  const int n = static_cast<int>(contours.size());
  if (n == 0) return result;
  std::vector<std::vector<ON_2dPoint>> polys(static_cast<size_t>(n));
  std::vector<double> areas(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    polys[static_cast<size_t>(i)] = ContourPolygon2d(contours[static_cast<size_t>(i)], plane);
    areas[static_cast<size_t>(i)] = PolygonAreaAbs(polys[static_cast<size_t>(i)]);
  }
  std::vector<int> depth(static_cast<size_t>(n), 0);
  for (int i = 0; i < n; ++i) {
    if (polys[static_cast<size_t>(i)].empty()) continue;
    for (int j = 0; j < n; ++j) {
      if (i == j || polys[static_cast<size_t>(j)].empty()) continue;
      if (kernel::PointInPolygon(polys[static_cast<size_t>(j)], polys[static_cast<size_t>(i)][0])) ++depth[static_cast<size_t>(i)];
    }
  }
  std::vector<int> parent(static_cast<size_t>(n), -1);
  for (int i = 0; i < n; ++i) {
    if (depth[static_cast<size_t>(i)] % 2 == 0) continue;  // solid material, not a hole
    int best = -1;
    double best_area = 0;
    for (int j = 0; j < n; ++j) {
      if (j == i || depth[static_cast<size_t>(j)] % 2 != 0) continue;
      if (!kernel::PointInPolygon(polys[static_cast<size_t>(j)], polys[static_cast<size_t>(i)][0])) continue;
      if (best == -1 || areas[static_cast<size_t>(j)] < best_area) { best = j; best_area = areas[static_cast<size_t>(j)]; }
    }
    parent[static_cast<size_t>(i)] = best;
  }

  const Vector3d direction = plane.zaxis * thickness;
  for (int i = 0; i < n; ++i) {
    if (depth[static_cast<size_t>(i)] != 0) continue;  // only depth-0 contours anchor a standalone solid
    std::vector<kernel::NurbsCurve> holes;
    for (int j = 0; j < n; ++j) {
      if (parent[static_cast<size_t>(j)] == i) holes.push_back(contours[static_cast<size_t>(j)]);
    }
    try {
      kernel::Brep b = holes.empty() ? kernel::Brep::Extrude(contours[static_cast<size_t>(i)], direction, /*cap=*/true)
                                     : kernel::ExtrudeProfileWithHoles(contours[static_cast<size_t>(i)], holes, direction);
      result.push_back(b);
    } catch (const std::exception& ex) {
      ctx.Warn(std::string("Text (Solids): skipped one glyph contour - ") + ex.what());
    }
  }
  return result;
}

// Adds text curves/surfaces as a group; returns the group id or -1.
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

// Adds standalone solid letters (parity-map "kernel: Feature operations" -
// "Lettering as solid geometry" - previously this command's own `Output`
// option only ever had Curves/Surfaces, no Solids/Thickness, exactly the
// gap this closes) as a group; returns the group id or -1. Each glyph -
// its outer contour plus whichever of its own counters BuildSolidLetters()
// (above) classifies as that glyph's holes - becomes one genuine capped
// solid Brep via kernel::Brep::Extrude()/kernel::ExtrudeProfileWithHoles(),
// not a curve or a bare (uncapped) surface.
int AddTextSolids(CommandContext& ctx, const std::string& text, double height, double thickness, const ON_Plane& plane,
                  const std::string& label) {
  ctx.Doc().BeginChange(label);
  std::string font;
  std::vector<kernel::NurbsCurve> curves;
  if (!TextToCurves(text, height, plane, curves, font)) {
    ctx.Warn("No TrueType font found for text outlines (looked for the system sans-serif fonts)");
    return -1;
  }
  const std::vector<kernel::Brep> letters = BuildSolidLetters(curves, plane, thickness, ctx);
  if (letters.empty()) { ctx.Warn(label + ": no solid letters could be built from this text"); return -1; }
  std::vector<ObjectId> ids;
  const std::string style = ctx.Settings().annotation_style;
  for (const kernel::Brep& b : letters) {
    SceneObject s = SceneObject::MakeBrep(b);
    TagAnnotation(s, label, style);
    s.user_text["Text"] = text;
    ids.push_back(ctx.Doc().Add(std::move(s)));
  }
  const int g = ctx.Doc().CreateGroup(ids, label);
  ctx.Print(label + ": " + std::to_string(ids.size()) + " solid(s) from " + std::filesystem::path(font).filename().string());
  return g;
}

double Height(CommandContext& ctx) { return AnnotationTextHeight(ctx); }

class TextCommand : public Command {
 public:
  explicit TextCommand(bool object) : object_(object) {}
  void Begin(CommandContext& ctx) override {
    height_ = Height(ctx);
    WantText("Text to create");
    options = {{"Height", FormatNumber(height_), {}, true, false}, {"Output", output_, {"Curves", "Surfaces", "Solids"}, false, false},
               {"Thickness", FormatNumber(thickness_), {}, true, false}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Height") { double h = std::atof(v.c_str()); if (h > 0) height_ = h; options[0].value = FormatNumber(height_); }
    if (n == "Output") { output_ = v; options[1].value = v; }
    if (n == "Thickness") { double t = std::atof(v.c_str()); if (t > 0) thickness_ = t; options[2].value = FormatNumber(thickness_); }
  }
  void OnText(CommandContext&, const std::string& t) override {
    if (text_.empty()) { text_ = t; WantPoint("Start point of text"); }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(p);
    const std::string label = object_ ? "TextObject" : "Text";
    if (output_ == "Solids") AddTextSolids(ctx, text_, height_, thickness_, pl, label);
    else AddTextCurves(ctx, text_, height_, pl, label, output_ == "Surfaces");
    Finish();
  }
  bool object_;
  std::string output_ = "Curves";
  double height_ = 1;
  double thickness_ = 0.2;
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
// (Struct itself, and the point-to-geometry math below, live in
// commands/DimGeometry.h now - shared verbatim with DXF/DWG DIMENSION
// import, io/FileExchange.cpp, which has no CommandContext to build a live
// dimension through.)

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
                              bool has_ref2, ObjectId ref2, const std::string& end2, double* len_out = nullptr,
                              const DimStyleParams& style = {}) {
  std::vector<kernel::NurbsCurve> curves;
  DimGlyphSpec dg;
  std::map<std::string, std::string> tags;
  if (!BuildLinearDimensionGeometry(p0, p1, L, text_h, curves, dg, tags, len_out, style)) return -1;
  if (has_ref1) { tags["DimRefObj1"] = std::to_string(ref1); tags["DimRefEnd1"] = end1; }
  if (has_ref2) { tags["DimRefObj2"] = std::to_string(ref2); tags["DimRefEnd2"] = end2; }
  GlyphSpec g;
  g.text = dg.text; g.height = dg.height; g.plane = dg.plane; g.center = dg.center;
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
// object; a curve's start/end/midpoint/interior knot; an arc/circle's
// center/quadrant; or a B-rep's vertex): see
// BuildLinearDimensionGroup/UpdateDimensions. A point that isn't on any
// object (e.g. picked in free space, or an object snap this build still
// doesn't resolve to an anchor, like Near/Perp/Tangent) still dimensions
// correctly, it just stays a static baked measurement, same as before this
// change.
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
    const AnnotationStyle& cur_style = ctx.Doc().CurrentAnnotationStyle();
    ctx.Doc().BeginChange(aligned_ ? "DimAligned" : "DimLinear");
    double len = 0;
    const int g = BuildLinearDimensionGroup(ctx, pts_[0], pts_[1], L, h, has1, ref1, end1, has2, ref2, end2, &len, StyleParamsOf(cur_style));
    if (g < 0) { Finish(); return; }
    ApplyStyleTolerance(ctx, g, cur_style);
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

// Builds (or rebuilds) one DimAngle group from its vertex + two direction
// points and a fixed plane - unlike the linear/radius layouts, everything
// else (arc radius, extension-line length, text position) is a pure
// function of those three points and the plane, so there is no separate
// "layout minus measured geometry" struct to round-trip: just the plane.
// The actual curve/text/measurement math now lives in
// commands/DimGeometry.h's BuildAngleDimensionGeometry (shared verbatim with
// DXF/DWG DIMENSION import, io/FileExchange.cpp, the same split
// BuildLinearDimensionGroup/BuildRadiusDimensionGroup above already use) -
// this wrapper just adds this command's own associativity tags on top.
// Tags: DimPlaneOrigin/X/Y (the plane), DimP0/DimP1/DimP2 (the three points
// as built, fallback), DimRefObj1/DimRefEnd1 (vertex), DimRefObj2/
// DimRefEnd2 (first direction point), DimRefObj3/DimRefEnd3 (second
// direction point) - present only where FindPointAnchor matched a real
// object, same associativity story as BuildLinearDimensionGroup. Returns -1
// if the two direction vectors are degenerate (same failure as before this
// change).
int BuildAngleDimensionGroup(CommandContext& ctx, Point3d vertex, Point3d p1, Point3d p2, const ON_Plane& pl, double text_h,
                             bool has0, ObjectId ref0, const std::string& end0,
                             bool has1, ObjectId ref1, const std::string& end1,
                             bool has2, ObjectId ref2, const std::string& end2, double* deg_out = nullptr,
                             int angular_precision = -1) {
  std::vector<kernel::NurbsCurve> curves;
  DimGlyphSpec dg;
  std::map<std::string, std::string> tags;
  if (!BuildAngleDimensionGeometry(vertex, p1, p2, pl, text_h, curves, dg, tags, deg_out, angular_precision)) return -1;
  if (has0) { tags["DimRefObj1"] = std::to_string(ref0); tags["DimRefEnd1"] = end0; }
  if (has1) { tags["DimRefObj2"] = std::to_string(ref1); tags["DimRefEnd2"] = end1; }
  if (has2) { tags["DimRefObj3"] = std::to_string(ref2); tags["DimRefEnd3"] = end2; }
  GlyphSpec g;
  g.text = dg.text; g.height = dg.height; g.plane = dg.plane; g.center = dg.center;
  return AddAnnotationGroup(ctx, "DimAngle", curves, g, -1, tags);
}

// Reads a DimAngle group's plane back (from any member).
bool LoadAngleDimPlane(Document& doc, int group_id, ON_Plane& pl) {
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    Point3d org, ax, ay;
    if (!o.user_text.count("DimPlaneOrigin") || !ParsePointTag(o.user_text.at("DimPlaneOrigin"), org)) continue;
    if (!o.user_text.count("DimPlaneX") || !ParsePointTag(o.user_text.at("DimPlaneX"), ax)) continue;
    if (!o.user_text.count("DimPlaneY") || !ParsePointTag(o.user_text.at("DimPlaneY"), ay)) continue;
    pl = ON_Plane(org, Vector3d(ax.x, ax.y, ax.z), Vector3d(ay.x, ay.y, ay.z));
    return true;
  }
  return false;
}

// Resolves a group's vertex/two direction points to their current value -
// same ref-then-fallback contract as ResolveLinearDimPoints, for each of the
// three points independently.
bool ResolveAngleDimPoints(Document& doc, int group_id, Point3d& v, Point3d& p1, Point3d& p2) {
  bool h0 = false, h1 = false, h2 = false;
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    if (!h0) {
      if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) {
        const ObjectId id = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10));
        const std::string end = o.user_text.count("DimRefEnd1") ? o.user_text.at("DimRefEnd1") : "point";
        if (ResolveAnchor(doc, id, end, v)) h0 = true;
      }
      if (!h0 && o.user_text.count("DimP0") && ParsePointTag(o.user_text.at("DimP0"), v)) h0 = true;
    }
    if (!h1) {
      if (auto it = o.user_text.find("DimRefObj2"); it != o.user_text.end()) {
        const ObjectId id = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10));
        const std::string end = o.user_text.count("DimRefEnd2") ? o.user_text.at("DimRefEnd2") : "point";
        if (ResolveAnchor(doc, id, end, p1)) h1 = true;
      }
      if (!h1 && o.user_text.count("DimP1") && ParsePointTag(o.user_text.at("DimP1"), p1)) h1 = true;
    }
    if (!h2) {
      if (auto it = o.user_text.find("DimRefObj3"); it != o.user_text.end()) {
        const ObjectId id = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10));
        const std::string end = o.user_text.count("DimRefEnd3") ? o.user_text.at("DimRefEnd3") : "point";
        if (ResolveAnchor(doc, id, end, p2)) h2 = true;
      }
      if (!h2 && o.user_text.count("DimP2") && ParsePointTag(o.user_text.at("DimP2"), p2)) h2 = true;
    }
    if (h0 && h1 && h2) return true;
  }
  return h0 && h1 && h2;
}

// Angle: vertex, first direction point, second direction point. Associative
// per-point exactly like DimLinear (FindPointAnchor on each of the three).
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
    const double h = Height(ctx);
    ObjectId r0 = kNoObject, r1 = kNoObject, r2 = kNoObject;
    std::string e0, e1, e2;
    const bool h0 = FindPointAnchor(ctx.Doc(), pts_[0], r0, e0);
    const bool h1 = FindPointAnchor(ctx.Doc(), pts_[1], r1, e1);
    const bool h2 = FindPointAnchor(ctx.Doc(), pts_[2], r2, e2);
    const AnnotationStyle& cur_style = ctx.Doc().CurrentAnnotationStyle();
    ctx.Doc().BeginChange("DimAngle");
    double deg = 0;
    const int g = BuildAngleDimensionGroup(ctx, pts_[0], pts_[1], pts_[2], pl, h, h0, r0, e0, h1, r1, e1, h2, r2, e2, &deg, cur_style.angular_precision);
    if (g < 0) { Finish(); return; }
    ApplyStyleTolerance(ctx, g, cur_style);
    const int nassoc = (h0 ? 1 : 0) + (h1 ? 1 : 0) + (h2 ? 1 : 0);
    ctx.Print("DimAngle " + Fmt(deg) + " deg" + (nassoc ? " (associative to " + std::to_string(nassoc) + " point(s))" : ""));
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { ctx.ClearPreview(); if (!pts_.empty()) ctx.AddPreviewLine(pts_[0], h); if (pts_.size() > 1) ctx.AddPreviewLine(pts_[0], pts_[1]); }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<Point3d> pts_;
};

// A DimRadius/DimDiameter dimension's fixed layout: everything about it that
// does not depend on the measured circle/arc's current center/radius, so it
// can be replayed against a fresh center/radius by UpdateDimensions when the
// measured curve moves or is resized. `dir` is the fixed world-space
// direction from center towards the dimension-line pick point (a one-time
// decision, like LinearDimLayout::horizontal); `extra` is how far beyond the
// (original) radius that pick point sat, so the leader end keeps the same
// visual stand-off as the radius changes. (Struct itself, and the
// point-to-geometry math below, live in commands/DimGeometry.h now - see
// BuildLinearDimensionGroup's comment above on why.)

// Builds (or rebuilds) one DimRadius/DimDiameter group from the measured
// circle/arc's center+radius and fixed layout. Tags mirror
// BuildLinearDimensionGroup's: DimIsDiameter/DimPlaneOrigin/DimPlaneX/
// DimPlaneY/DimDir/DimExtra are the layout (LoadRadiusDimLayout), DimCenter/
// DimRadiusVal are the as-built fallback, and DimRefObj1 - present only when
// the command was run on a real selected curve, which is always, since
// DimRadius/DimDiameter require picking an object - is the associativity:
// ResolveRadiusDimGeom re-evaluates it via ResolveArcAnchor instead of the
// fallback tags when it still resolves to an arc/circle.
int BuildRadiusDimensionGroup(CommandContext& ctx, Point3d center, double radius, const RadiusDimLayout& L, double text_h,
                              bool has_ref, ObjectId ref, double* val_out = nullptr, const DimStyleParams& style = {}) {
  std::vector<kernel::NurbsCurve> curves;
  DimGlyphSpec dg;
  std::map<std::string, std::string> tags;
  if (!BuildRadiusDimensionGeometry(center, radius, L, text_h, curves, dg, tags, val_out, style)) return -1;
  if (has_ref) tags["DimRefObj1"] = std::to_string(ref);
  GlyphSpec g;
  g.text = dg.text; g.height = dg.height; g.plane = dg.plane; g.center = dg.center;
  return AddAnnotationGroup(ctx, L.diameter ? "DimDiameter" : "DimRadius", curves, g, -1, tags);
}

// Reads a DimRadius/DimDiameter group's layout tags back.
bool LoadRadiusDimLayout(Document& doc, int group_id, RadiusDimLayout& L) {
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    Point3d org, ax, ay, dir;
    if (!o.user_text.count("DimPlaneOrigin") || !ParsePointTag(o.user_text.at("DimPlaneOrigin"), org)) continue;
    if (!o.user_text.count("DimPlaneX") || !ParsePointTag(o.user_text.at("DimPlaneX"), ax)) continue;
    if (!o.user_text.count("DimPlaneY") || !ParsePointTag(o.user_text.at("DimPlaneY"), ay)) continue;
    if (!o.user_text.count("DimDir") || !ParsePointTag(o.user_text.at("DimDir"), dir)) continue;
    L.plane = ON_Plane(org, Vector3d(ax.x, ax.y, ax.z), Vector3d(ay.x, ay.y, ay.z));
    L.dir = Vector3d(dir.x, dir.y, dir.z);
    L.diameter = o.user_text.count("DimIsDiameter") && o.user_text.at("DimIsDiameter") == "1";
    L.extra = o.user_text.count("DimExtra") ? std::atof(o.user_text.at("DimExtra").c_str()) : 0.0;
    return true;
  }
  return false;
}

// Resolves a group's measured center/radius to their *current* value: the
// referenced curve's live arc/circle geometry when DimRefObj1 resolves via
// ResolveArcAnchor, else the center/radius recorded at creation (DimCenter/
// DimRadiusVal).
bool ResolveRadiusDimGeom(Document& doc, int group_id, Point3d& center, double& radius) {
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) {
      const ObjectId id = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10));
      ON_Arc arc;
      if (ResolveArcAnchor(doc, id, arc)) { center = arc.Center(); radius = arc.Radius(); return true; }
    }
    if (o.user_text.count("DimCenter") && o.user_text.count("DimRadiusVal") && ParsePointTag(o.user_text.at("DimCenter"), center)) {
      radius = std::atof(o.user_text.at("DimRadiusVal").c_str());
      return true;
    }
  }
  return false;
}

// Radius/diameter: select an arc or circle, then place the leader. Always
// associative (see BuildRadiusDimensionGroup) since the command requires a
// real selected curve to measure in the first place - unlike DimLinear,
// there is no free-floating-point case here.
class DimRadiusCommand : public Command {
 public:
  explicit DimRadiusCommand(bool diameter) : diameter_(diameter) {}
  void Begin(CommandContext&) override { WantObjects("Select an arc or circle"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      ON_Arc arc;
      if (o && o->kind == ObjectKind::Curve && o->curve->raw().IsArc(nullptr, &arc)) { arc_ = arc; obj_ = id; have_ = true; break; }
    }
    if (!have_) { ctx.Warn("Select an arc or circle"); Finish(); return; }
    WantPoint("Dimension location");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    const ON_Plane pl = ActivePlane(ctx);
    Vector3d d = p - arc_.Center();
    const double extra = d.Length() - arc_.Radius();
    if (!d.Unitize()) d = pl.xaxis;
    RadiusDimLayout L;
    L.diameter = diameter_;
    L.plane = pl;
    L.dir = d;
    L.extra = extra;
    const double h = Height(ctx);
    const AnnotationStyle& cur_style = ctx.Doc().CurrentAnnotationStyle();
    ctx.Doc().BeginChange(diameter_ ? "DimDiameter" : "DimRadius");
    double val = 0;
    const int g = BuildRadiusDimensionGroup(ctx, arc_.Center(), arc_.Radius(), L, h, true, obj_, &val, StyleParamsOf(cur_style));
    if (g < 0) { Finish(); return; }
    ApplyStyleTolerance(ctx, g, cur_style);
    ctx.Print(std::string(diameter_ ? "DimDiameter " : "DimRadius ") + Fmt(val) + " (associative to selected arc/circle)");
    Finish();
  }
  bool diameter_;
  bool have_ = false;
  ObjectId obj_ = kNoObject;
  ON_Arc arc_;
};

// Builds (or rebuilds) one Leader group from its arrowhead point (`tip`)
// plus the rest of its polyline stored as fixed offsets *from* the tip -
// so when the tip's anchor moves, the whole leader shape translates with it
// rather than needing to re-derive a bend shape from nothing. The actual
// curve/text math now lives in commands/DimGeometry.h's
// BuildLeaderGeometry (shared with native `.3dm` `ON_Leader` import,
// io/File3dm.cpp, the same split BuildLinearDimensionGroup/
// BuildRadiusDimensionGroup/BuildAngleDimensionGroup above already use) -
// this wrapper just adds this command's own associativity tag on top.
// Tags: DimPlaneOrigin/X/Y (plane orientation), LeaderTip (fallback tip),
// LeaderRest (";"-joined offsets), DimRefObj1/DimRefEnd1 (the tip's
// FindPointAnchor match, when any).
int BuildLeaderGroup(CommandContext& ctx, Point3d tip, const std::vector<Vector3d>& rest_offsets, const ON_Plane& pl, double text_h,
                     const std::string& text, bool has_ref, ObjectId ref, const std::string& end) {
  std::vector<kernel::NurbsCurve> curves;
  DimGlyphSpec dg;
  std::map<std::string, std::string> tags;
  if (!BuildLeaderGeometry(tip, rest_offsets, pl, text_h, text, curves, dg, tags)) return -1;
  if (has_ref) { tags["DimRefObj1"] = std::to_string(ref); tags["DimRefEnd1"] = end; }
  GlyphSpec g;
  g.text = dg.text; g.height = dg.height; g.plane = dg.plane; g.center = dg.center;
  return AddAnnotationGroup(ctx, "Leader", curves, g, -1, tags);
}

// Reads a Leader group's plane orientation and bend-point offsets back.
bool LoadLeaderLayout(Document& doc, int group_id, ON_Plane& pl, std::vector<Vector3d>& rest_offsets) {
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    Point3d org, ax, ay;
    if (!o.user_text.count("DimPlaneOrigin") || !ParsePointTag(o.user_text.at("DimPlaneOrigin"), org)) continue;
    if (!o.user_text.count("DimPlaneX") || !ParsePointTag(o.user_text.at("DimPlaneX"), ax)) continue;
    if (!o.user_text.count("DimPlaneY") || !ParsePointTag(o.user_text.at("DimPlaneY"), ay)) continue;
    pl = ON_Plane(org, Vector3d(ax.x, ax.y, ax.z), Vector3d(ay.x, ay.y, ay.z));
    rest_offsets.clear();
    if (auto it = o.user_text.find("LeaderRest"); it != o.user_text.end()) {
      std::stringstream ss(it->second);
      std::string tok;
      while (std::getline(ss, tok, ';')) {
        Point3d off;
        if (!tok.empty() && ParsePointTag(tok, off)) rest_offsets.push_back(Vector3d(off.x, off.y, off.z));
      }
    }
    return true;
  }
  return false;
}

// Resolves a Leader group's tip to its current value: the referenced
// object's live position when DimRefObj1 resolves, else the LeaderTip
// recorded at creation.
bool ResolveLeaderTip(Document& doc, int group_id, Point3d& tip) {
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) {
      const ObjectId id = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10));
      const std::string end = o.user_text.count("DimRefEnd1") ? o.user_text.at("DimRefEnd1") : "point";
      if (ResolveAnchor(doc, id, end, tip)) return true;
    }
    if (o.user_text.count("LeaderTip") && ParsePointTag(o.user_text.at("LeaderTip"), tip)) return true;
  }
  return false;
}

// Leader: arrowhead point, any number of bend points, then text.
// Associative when the arrowhead sits exactly on a real object
// (FindPointAnchor, same coincidence rule as DimLinear's endpoints): the
// whole leader shape (bend points, text) translates with the tip.
class LeaderCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Start of leader (arrowhead)"); }
  void OnPoint(CommandContext& ctx, Point3d p) override { pts_.push_back(p); ctx.SetLastPoint(p); WantPoint("Next point. Press Enter for text"); }
  void OnEnter(CommandContext&) override { if (pts_.size() >= 2) WantText("Leader text"); else Finish(); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    ctx.ClearPreview();
    const ON_Plane pl = ActivePlane(ctx);
    const double h = Height(ctx);
    ObjectId ref = kNoObject;
    std::string end;
    const bool has_ref = FindPointAnchor(ctx.Doc(), pts_[0], ref, end);
    std::vector<Vector3d> rest;
    for (size_t i = 1; i < pts_.size(); ++i) rest.push_back(pts_[i] - pts_[0]);
    ctx.Doc().BeginChange("Leader");
    const int g = BuildLeaderGroup(ctx, pts_[0], rest, pl, h, t, has_ref, ref, end);
    if (g < 0) { Finish(); return; }
    ctx.Print("Leader " + t + (has_ref ? " (associative to arrowhead point)" : ""));
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
  const char* linear_dim_note =
      "Bakes curve/arrow geometry like every dimension here, but is associative when a measured point sits exactly on a real object - a Point object; a curve's start/end, segment midpoint, arc-length midpoint or interior knot point; an arc/circle's center or quadrant point; or a B-rep's vertex (see FindPointAnchor, annotate_common.h, matching the viewport's own End/Mid/Knot/Cen/Quad/Vertex osnaps) - the dimension records which object and anchor it measured, and UpdateDimensions re-evaluates that object's current position and redraws the dimension line/text from it. A point that isn't on any object (free space, or a snap this build still doesn't resolve to an anchor, like Near/Perp/Tangent) still dimensions correctly but stays a static baked measurement for that endpoint, same as before this change. DimRotated builds the identical dimension as DimAligned (same command, same tagging), so it shares this associativity too. Its current AnnotationStyle's precision/unit_suffix/ext_offset/ext_extension/text_placement/tolerance fields (Document.h) are real and wired in (DimStyleParams, DimGeometry.h/annotate_common.h): the measured text's decimal places and unit suffix, the extension lines' gap-from-point and overshoot-past-the-dimension-line, whether the line breaks for a centered text or stays unbroken with text above it, and an optional default tolerance suffix (the same suffix format the manual DimTolerance command builds) are all real per-style properties now, not stored-and-ignored ones - UpdateDimensions re-reads them from the dimension's own recorded style by name, so editing a style and re-running Update picks up the change.";
  const char* angle_dim_note =
      "Bakes curve/arrow geometry, associative per point exactly like DimLinear (FindPointAnchor on the vertex and each of the two direction points - see annotate_common.h): UpdateDimensions re-evaluates whichever of the three points matched a real object and rebuilds the arc/extension-lines/text from their current positions. A point that isn't on any object stays a static baked measurement for that vertex, same as before this change. Its style's angular_precision and default tolerance (if any) are real and wired in, the same way linear_dim_note describes.";
  const char* radius_dim_note =
      "Bakes curve/arrow geometry, but is always associative: DimRadius/DimDiameter require selecting a real arc/circle curve to measure, so that curve's id is recorded directly (not by coincident-point matching) and UpdateDimensions re-evaluates its current center/radius (ResolveArcAnchor, annotate_common.h) and rebuilds the leader/text from it - the dimension-line direction and stand-off distance chosen at creation are kept fixed as the circle/arc moves or resizes. Its style's precision/unit_suffix and default tolerance (if any) are real and wired in, the same way linear_dim_note describes (extension-line/text-placement fields don't apply to a radius/diameter leader).";
  const char* leader_dim_note =
      "Bakes curve/arrow geometry, associative when the arrowhead point sits exactly on a real object (FindPointAnchor, same coincidence rule as DimLinear, including its curve-midpoint/knot/arc-center/quadrant/B-rep-vertex anchors): UpdateDimensions re-evaluates that object's current position and redraws the whole leader (bend points and text keep their built offsets from the tip, so the shape translates with it) - a leader whose arrowhead isn't on any object stays a static baked leader, same as before this change.";
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
  Reg(e, "DimRotated", Make<DimLinearCommand>(true), CommandStatus::Implemented, linear_dim_note);
  Reg(e, "DimAngle", Make<DimAngleCommand>(), CommandStatus::Implemented, angle_dim_note);
  Reg(e, "DimRadius", Make<DimRadiusCommand>(false), CommandStatus::Implemented, radius_dim_note);
  Reg(e, "DimDiameter", Make<DimRadiusCommand>(true), CommandStatus::Implemented, radius_dim_note);
  Reg(e, "Leader", Make<LeaderCommand>(), CommandStatus::Implemented, leader_dim_note);

  Reg(e, "UpdateDimensions", Immediate([](CommandContext& ctx) {
        static const std::vector<std::string> kKinds = {"DimLinear", "DimAligned", "DimAngle", "DimRadius", "DimDiameter", "Leader", "Centermark", "CenterLine"};
        std::vector<int> groups;
        std::map<int, std::string> kind_of;
        for (const SceneObject& o : ctx.Doc().Objects()) {
          auto it = o.user_text.find("Annotation");
          if (it == o.user_text.end() || std::find(kKinds.begin(), kKinds.end(), it->second) == kKinds.end()) continue;
          if (o.group_id >= 0 && !kind_of.count(o.group_id)) { kind_of[o.group_id] = it->second; groups.push_back(o.group_id); }
        }
        if (groups.empty()) { ctx.Print("UpdateDimensions: no associative dimensions in this document"); return; }
        ctx.Doc().BeginChange("UpdateDimensions");
        int updated = 0, skipped = 0;
        for (int g : groups) {
          const std::string& kind = kind_of[g];
          if (kind == "DimLinear" || kind == "DimAligned") {
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
            const std::string tol = GroupToleranceSuffix(ctx, g, old_glyph);
            const DimStyleParams dim_style = StyleParamsByName(ctx.Doc(), GroupStyleName(ctx.Doc(), g));
            for (ObjectId id : ctx.Doc().GroupMembers(g)) ctx.Doc().Remove(id);
            double len = 0;
            const int new_g = BuildLinearDimensionGroup(ctx, p0, p1, L, h, has1, ref1, end1, has2, ref2, end2, &len, dim_style);
            if (new_g >= 0) {
              ReapplyToleranceSuffix(ctx, new_g, tol);
              ++updated;
              ctx.Print("UpdateDimensions:   " + std::string(L.aligned ? "DimAligned" : "DimLinear") + " now measures " + Fmt(len));
            } else ++skipped;
          } else if (kind == "DimAngle") {
            ON_Plane pl;
            Point3d v, p1, p2;
            if (!LoadAngleDimPlane(ctx.Doc(), g, pl) || !ResolveAngleDimPoints(ctx.Doc(), g, v, p1, p2)) { ++skipped; continue; }
            GlyphSpec old_glyph;
            const double h = GroupGlyphSpec(ctx, g, old_glyph) ? old_glyph.height : AnnotationTextHeight(ctx);
            ObjectId r0 = kNoObject, r1 = kNoObject, r2 = kNoObject;
            std::string e0, e1, e2;
            bool h0 = false, h1 = false, h2 = false;
            for (const SceneObject& o : ctx.Doc().Objects()) {
              if (o.group_id != g) continue;
              if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) { r0 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); e0 = o.user_text.count("DimRefEnd1") ? o.user_text.at("DimRefEnd1") : "point"; h0 = true; }
              if (auto it = o.user_text.find("DimRefObj2"); it != o.user_text.end()) { r1 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); e1 = o.user_text.count("DimRefEnd2") ? o.user_text.at("DimRefEnd2") : "point"; h1 = true; }
              if (auto it = o.user_text.find("DimRefObj3"); it != o.user_text.end()) { r2 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); e2 = o.user_text.count("DimRefEnd3") ? o.user_text.at("DimRefEnd3") : "point"; h2 = true; }
            }
            const std::string tol = GroupToleranceSuffix(ctx, g, old_glyph);
            int angular_precision = -1;
            {
              const std::string style_name = GroupStyleName(ctx.Doc(), g);
              if (const AnnotationStyle* st = ctx.Doc().FindAnnotationStyle(style_name)) angular_precision = st->angular_precision;
            }
            for (ObjectId id : ctx.Doc().GroupMembers(g)) ctx.Doc().Remove(id);
            double deg = 0;
            const int new_g = BuildAngleDimensionGroup(ctx, v, p1, p2, pl, h, h0, r0, e0, h1, r1, e1, h2, r2, e2, &deg, angular_precision);
            if (new_g >= 0) {
              ReapplyToleranceSuffix(ctx, new_g, tol);
              ++updated;
              ctx.Print("UpdateDimensions:   DimAngle now measures " + Fmt(deg) + " deg");
            } else ++skipped;
          } else if (kind == "DimRadius" || kind == "DimDiameter") {
            RadiusDimLayout L;
            Point3d center; double radius = 0;
            if (!LoadRadiusDimLayout(ctx.Doc(), g, L) || !ResolveRadiusDimGeom(ctx.Doc(), g, center, radius)) { ++skipped; continue; }
            GlyphSpec old_glyph;
            const double h = GroupGlyphSpec(ctx, g, old_glyph) ? old_glyph.height : AnnotationTextHeight(ctx);
            ObjectId ref1 = kNoObject;
            bool has1 = false;
            for (const SceneObject& o : ctx.Doc().Objects()) {
              if (o.group_id != g) continue;
              if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) { ref1 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); has1 = true; }
            }
            const std::string tol = GroupToleranceSuffix(ctx, g, old_glyph);
            const DimStyleParams dim_style = StyleParamsByName(ctx.Doc(), GroupStyleName(ctx.Doc(), g));
            for (ObjectId id : ctx.Doc().GroupMembers(g)) ctx.Doc().Remove(id);
            double val = 0;
            const int new_g = BuildRadiusDimensionGroup(ctx, center, radius, L, h, has1, ref1, &val, dim_style);
            if (new_g >= 0) {
              ReapplyToleranceSuffix(ctx, new_g, tol);
              ++updated;
              ctx.Print("UpdateDimensions:   " + kind + " now measures " + Fmt(val));
            } else ++skipped;
          } else if (kind == "Leader") {
            ON_Plane pl;
            std::vector<Vector3d> rest;
            Point3d tip;
            if (!LoadLeaderLayout(ctx.Doc(), g, pl, rest) || !ResolveLeaderTip(ctx.Doc(), g, tip)) { ++skipped; continue; }
            GlyphSpec old_glyph;
            std::string text = "Leader";
            double h = AnnotationTextHeight(ctx);
            if (GroupGlyphSpec(ctx, g, old_glyph)) { text = old_glyph.text; h = old_glyph.height; }
            ObjectId ref1 = kNoObject;
            std::string end1;
            bool has1 = false;
            for (const SceneObject& o : ctx.Doc().Objects()) {
              if (o.group_id != g) continue;
              if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) { ref1 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); end1 = o.user_text.count("DimRefEnd1") ? o.user_text.at("DimRefEnd1") : "point"; has1 = true; }
            }
            for (ObjectId id : ctx.Doc().GroupMembers(g)) ctx.Doc().Remove(id);
            if (BuildLeaderGroup(ctx, tip, rest, pl, h, text, has1, ref1, end1) >= 0) {
              ++updated;
              ctx.Print("UpdateDimensions:   Leader now points at " + PointTag(tip));
            } else ++skipped;
          } else if (kind == "Centermark") {
            Point3d center; ON_Plane pl; double size = 0;
            if (!ResolveCentermarkGeom(ctx.Doc(), g, center, pl, size)) { ++skipped; continue; }
            std::string mode = "Fixed";
            bool has_ref = false;
            ObjectId ref = kNoObject;
            int layer = -1;
            for (const SceneObject& o : ctx.Doc().Objects()) {
              if (o.group_id != g) continue;
              layer = o.layer_index;
              if (auto it = o.user_text.find("CenterSizeMode"); it != o.user_text.end()) mode = it->second;
              if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) { ref = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); has_ref = true; }
            }
            for (ObjectId id : ctx.Doc().GroupMembers(g)) ctx.Doc().Remove(id);
            if (BuildCentermarkGroup(ctx, center, pl, size, layer, has_ref, ref, mode) >= 0) {
              ++updated;
              ctx.Print("UpdateDimensions:   Centermark now at " + PointTag(center));
            } else ++skipped;
          } else if (kind == "CenterLine") {
            Point3d m0, m1;
            if (!ResolveCenterLinePoints(ctx.Doc(), g, m0, m1)) { ++skipped; continue; }
            ObjectId ref1 = kNoObject, ref2 = kNoObject;
            bool has1 = false, has2 = false;
            int layer = -1;
            for (const SceneObject& o : ctx.Doc().Objects()) {
              if (o.group_id != g) continue;
              layer = o.layer_index;
              if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) { ref1 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); has1 = true; }
              if (auto it = o.user_text.find("DimRefObj2"); it != o.user_text.end()) { ref2 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); has2 = true; }
            }
            for (ObjectId id : ctx.Doc().GroupMembers(g)) ctx.Doc().Remove(id);
            if (has1 && has2 && BuildCenterLineGroup(ctx, m0, m1, layer, ref1, ref2) >= 0) {
              ++updated;
              ctx.Print("UpdateDimensions:   CenterLine now spans " + PointTag(m0) + " to " + PointTag(m1));
            } else ++skipped;
          }
        }
        ctx.Print("UpdateDimensions: " + std::to_string(updated) + " dimension(s) regenerated" + (skipped ? ", " + std::to_string(skipped) + " skipped (no resolvable layout/points)" : ""));
      }), CommandStatus::Implemented,
      "Re-evaluates every associative dimension's anchor(s) - DimLinear/DimAligned/DimRotated, DimAngle, DimRadius/DimDiameter, Leader, Centermark, CenterLine - and rebuilds its curve/arrow/text geometry from their current position, replacing the old baked geometry in place - the associative counterpart to those dimension types' static bake, following the same explicit-recompute shape as UpdateSectionViews (cmd_drafting2.cpp) rather than an automatic hook on every document edit. This window fixes a gap in that rebuild for DimLinear/DimAligned, DimAngle and DimRadius/DimDiameter: a DimTolerance suffix (cmd_drafting2.cpp) on one of these used to be silently dropped, since the rebuilt text was always the freshly recomputed measurement alone. It is now diffed against the DimTolerance.Base tag DimTolerance itself leaves and re-appended to whatever the new measurement is (GroupToleranceSuffix/ReapplyToleranceSuffix, annotate_common.h) - a dimension carrying a tolerance keeps it through a move/edit, not just through the DimTolerance command's own idempotent re-run. Leader was never affected (its text is a static label copied verbatim, not recomputed), and Centermark/CenterLine have no text to lose. A later window wires DimLinear/DimAligned/DimAngle/DimRadius/DimDiameter's rebuild to the dimension's own recorded AnnotationStyle by name (StyleParamsByName, annotate_common.h): the style's precision/unit_suffix/ext_offset/ext_extension/text_placement are re-read live on every Update, not just baked in at creation, so editing a style's dimension properties and re-running UpdateDimensions reformats every dimension built with it.");
}

}  // namespace dino8::app
