// Annotation helpers shared by cmd_annotate.cpp (Text, Dim*, Leader) and
// cmd_annotate2.cpp (DimArea, TextProperties, FindText...).
//
// Annotations are groups of curves. Every member carries user text:
//   Annotation   the command that made it (Text, DimLinear, Leader, DimArea...)
//   Style        the annotation style name it was made with
// and every glyph (text outline) curve additionally:
//   Glyph        "1"
//   Text         the string
//   TextHeight   capital height in model units
//   TextOrigin   "x,y,z" of the text anchor
//   TextX/TextY  "x,y,z" plane axes of the text
//   TextAlign    Left or Center (about the anchor)
// so the text can be rebuilt after an edit (TextProperties, ScaleTextHeight).
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "commands/cmd_common.h"
#include "geom/TextOutline.h"

namespace dino8::app {

// Layer new dimensions go on: SetDimensionLayer's layer if it exists, else
// the current layer.
inline int DimensionLayer(CommandContext& ctx) {
  const std::string& name = ctx.Settings().dimension_layer;
  if (!name.empty()) {
    const int idx = ctx.Doc().FindLayer(name);
    if (idx >= 0) return idx;
  }
  return ctx.Doc().CurrentLayer();
}

// Layer new Centermark/CenterLine objects go on: SetCenterLayer's layer
// (CENTERLAYER) if it exists, else the current layer - mirrors
// DimensionLayer/SetDimensionLayer above.
inline int CenterLayer(CommandContext& ctx) {
  const std::string& name = ctx.Settings().center_layer;
  if (!name.empty()) {
    const int idx = ctx.Doc().FindLayer(name);
    if (idx >= 0) return idx;
  }
  return ctx.Doc().CurrentLayer();
}

// Default text height: the current annotation style's, else twice the grid spacing.
inline double AnnotationTextHeight(CommandContext& ctx) {
  const AnnotationStyle& st = ctx.Doc().CurrentAnnotationStyle();
  if (st.text_height > 0) return st.text_height;
  return std::max(ctx.Settings().grid_spacing * 2.0, 1e-6);
}

inline double AnnotationArrowSize(CommandContext& ctx) {
  const AnnotationStyle& st = ctx.Doc().CurrentAnnotationStyle();
  return st.arrow_size > 0 ? st.arrow_size : AnnotationTextHeight(ctx);
}

inline std::string PointTag(Point3d p) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%.10g,%.10g,%.10g", p.x, p.y, p.z);
  return buf;
}

inline bool ParsePointTag(const std::string& s, Point3d& out) {
  double x = 0, y = 0, z = 0;
  if (std::sscanf(s.c_str(), "%lf,%lf,%lf", &x, &y, &z) != 3) return false;
  out = Point3d(x, y, z);
  return true;
}

struct GlyphSpec {
  std::string text;
  double height = 1;
  ON_Plane plane;          // origin = anchor
  bool center = false;     // centre the text on the anchor (dimensions)
};

inline void TagAnnotation(SceneObject& o, const std::string& kind, const std::string& style) {
  o.user_text["Annotation"] = kind;
  o.user_text["Style"] = style;
}

inline void TagGlyph(SceneObject& o, const GlyphSpec& g) {
  o.user_text["Glyph"] = "1";
  o.user_text["Text"] = g.text;
  o.user_text["TextHeight"] = FormatNumber(g.height);
  o.user_text["TextOrigin"] = PointTag(g.plane.origin);
  o.user_text["TextX"] = PointTag(Point3d(g.plane.xaxis));
  o.user_text["TextY"] = PointTag(Point3d(g.plane.yaxis));
  o.user_text["TextAlign"] = g.center ? "Center" : "Left";
}

// Reads a glyph's spec back from its tags.
inline bool GlyphSpecOf(const SceneObject& o, GlyphSpec& g) {
  auto get = [&](const char* k) -> const std::string* { auto it = o.user_text.find(k); return it == o.user_text.end() ? nullptr : &it->second; };
  const std::string *t = get("Text"), *h = get("TextHeight"), *org = get("TextOrigin"), *x = get("TextX"), *y = get("TextY"), *al = get("TextAlign");
  if (!t || !h || !org || !x || !y) return false;
  Point3d o3, px, py;
  if (!ParsePointTag(*org, o3) || !ParsePointTag(*x, px) || !ParsePointTag(*y, py)) return false;
  g.text = *t;
  g.height = std::atof(h->c_str());
  g.plane = ON_Plane(o3, Vector3d(px.x, px.y, px.z), Vector3d(py.x, py.y, py.z));
  g.center = al && *al == "Center";
  return g.height > 0;
}

// Adds the glyph curves of `g` to the document (tagged) and returns their ids.
// `like` supplies layer / group / annotation tags to copy.
inline std::vector<ObjectId> AddGlyphCurves(CommandContext& ctx, const GlyphSpec& g, int layer, int group_id,
                                            const std::map<std::string, std::string>& extra_tags, std::string* font_used = nullptr) {
  std::vector<ObjectId> ids;
  std::vector<kernel::NurbsCurve> glyphs;
  std::string font;
  double width = 0;
  if (!TextToCurves(g.text, g.height, g.plane, glyphs, font, &width)) {
    ctx.Warn("No TrueType font found for text outlines (looked for the system sans-serif fonts)");
    return ids;
  }
  if (font_used) *font_used = font;
  const ON_Xform shift = ON_Xform::TranslationTransformation(-g.plane.xaxis * (g.center ? width / 2 : 0));
  for (kernel::NurbsCurve& c : glyphs) {
    if (g.center) c.raw().Transform(shift);
    SceneObject s = SceneObject::MakeCurve(c);
    s.layer_index = layer;
    s.group_id = group_id;
    for (const auto& [k, v] : extra_tags) s.user_text[k] = v;
    TagGlyph(s, g);
    ids.push_back(ctx.Doc().Add(std::move(s)));
  }
  return ids;
}

// Adds a complete annotation: `curves` (lines, arrows, leaders) plus the
// text, as one group on the dimension layer. `extra_tags` (e.g. a
// dimension's DimRefObj1/DimRefEnd1 anchors - see cmd_annotate.cpp) is
// copied onto every curve in the group, so a later associativity update can
// read it back from any member without needing to know which one holds it.
// Returns the group id or -1.
inline int AddAnnotationGroup(CommandContext& ctx, const std::string& kind, const std::vector<kernel::NurbsCurve>& curves,
                              const GlyphSpec& text, int layer = -1, const std::map<std::string, std::string>& extra_tags = {}) {
  if (layer < 0) layer = DimensionLayer(ctx);
  const std::string style = ctx.Settings().annotation_style;
  std::vector<ObjectId> ids;
  for (const kernel::NurbsCurve& c : curves) {
    SceneObject s = SceneObject::MakeCurve(c);
    s.layer_index = layer;
    TagAnnotation(s, kind, style);
    for (const auto& [k, v] : extra_tags) s.user_text[k] = v;
    ids.push_back(ctx.Doc().Add(std::move(s)));
  }
  if (!text.text.empty()) {
    for (ObjectId id : AddGlyphCurves(ctx, text, layer, -1, {{"Annotation", kind}, {"Style", style}})) ids.push_back(id);
  }
  if (ids.empty()) return -1;
  return ctx.Doc().CreateGroup(ids, kind);
}

// Replaces the glyph curves of an annotation group with a rebuilt text.
// `extra_keys` names additional user_text keys to carry over unchanged from
// the old glyph to the new one (beyond "Annotation"/"Style", always kept) -
// e.g. a command that stamps its own state onto the glyph (DimTolerance's
// pre-tolerance base text) so a second run can read it back instead of
// compounding onto whatever text is currently displayed.
// Returns the number of glyph curves made.
inline int RebuildGroupText(CommandContext& ctx, int group_id, const GlyphSpec& g, const std::vector<std::string>& extra_keys = {}) {
  std::vector<ObjectId> old;
  int layer = -1;
  std::map<std::string, std::string> tags;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (o.group_id != group_id || !o.user_text.count("Glyph")) continue;
    old.push_back(o.id);
    layer = o.layer_index;
    for (const char* k : {"Annotation", "Style"}) { auto it = o.user_text.find(k); if (it != o.user_text.end()) tags[k] = it->second; }
    for (const std::string& k : extra_keys) { auto it = o.user_text.find(k); if (it != o.user_text.end()) tags[k] = it->second; }
  }
  if (old.empty()) return 0;
  for (ObjectId id : old) ctx.Doc().Remove(id);
  return static_cast<int>(AddGlyphCurves(ctx, g, layer, group_id, tags).size());
}

// The distinct annotation groups among `ids` (objects tagged Annotation).
inline std::vector<int> AnnotationGroupsOf(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<int> groups;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->group_id < 0 || !o->user_text.count("Annotation")) continue;
    if (std::find(groups.begin(), groups.end(), o->group_id) == groups.end()) groups.push_back(o->group_id);
  }
  return groups;
}

// The glyph spec of a group (from its first glyph), if any.
inline bool GroupGlyphSpec(CommandContext& ctx, int group_id, GlyphSpec& g) {
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (o.group_id == group_id && o.user_text.count("Glyph") && GlyphSpecOf(o, g)) return true;
  }
  return false;
}

// Finds a real document object anchored exactly at `p` (a Point object at
// that location, or a curve's start/end): the basis for associative
// dimensions (DimLinear/DimAligned - cmd_annotate.cpp). Skips other
// annotation output (a dimension should never anchor to another dimension's
// baked geometry) and returns false when `p` is free-floating - a dimension
// built from unanchored points stays a static baked measurement, same as
// today, since there is nothing live to track it back to.
inline bool FindPointAnchor(Document& doc, Point3d p, ObjectId& obj, std::string& which) {
  const double eps = 1e-7;
  for (const SceneObject& o : doc.Objects()) {
    if (o.user_text.count("Annotation")) continue;
    if (o.kind == ObjectKind::Point) {
      if (o.point.DistanceTo(p) < eps) { obj = o.id; which = "point"; return true; }
    } else if (o.kind == ObjectKind::Curve && o.curve) {
      if (o.curve->raw().PointAtStart().DistanceTo(p) < eps) { obj = o.id; which = "start"; return true; }
      if (o.curve->raw().PointAtEnd().DistanceTo(p) < eps) { obj = o.id; which = "end"; return true; }
    }
  }
  return false;
}

// Resolves an anchor made by FindPointAnchor back to a current point -
// the object's *current* location, which is how a moved/edited source
// object propagates into a dimension update. False if the object is gone
// or no longer the kind the anchor expects (e.g. a curve turned into
// something else by a boolean/edit that replaced it).
inline bool ResolveAnchor(Document& doc, ObjectId obj, const std::string& which, Point3d& out) {
  const SceneObject* o = doc.Find(obj);
  if (!o) return false;
  if (which == "point") { if (o->kind != ObjectKind::Point) return false; out = o->point; return true; }
  if (o->kind != ObjectKind::Curve || !o->curve) return false;
  out = which == "start" ? Point3d(o->curve->raw().PointAtStart()) : Point3d(o->curve->raw().PointAtEnd());
  return true;
}

// Resolves a whole-object anchor (DimRadius/DimDiameter's measured circle or
// arc, selected directly rather than matched by coincident point like
// FindPointAnchor) back to its *current* arc/circle geometry. False if the
// object is gone or no longer a curve, or the curve is no longer arc-shaped
// (e.g. turned non-planar by an edit) - same "falls back to the static bake"
// contract as ResolveAnchor.
inline bool ResolveArcAnchor(Document& doc, ObjectId obj, ON_Arc& out) {
  const SceneObject* o = doc.Find(obj);
  if (!o || o->kind != ObjectKind::Curve || !o->curve) return false;
  ON_Arc arc;
  if (!o->curve->raw().IsArc(nullptr, &arc)) return false;
  out = arc;
  return true;
}

// Centermark/CenterLine associativity (cmd_annotate2.cpp's CentermarkCommand
// /CenterLineCommand build these; UpdateDimensions in cmd_annotate.cpp
// re-evaluates them - shared here for the same reason BuildLinearDimension-
// Group's comment gives for DimGeometry.h).

// Builds (or rebuilds) one Centermark group from its resolved center/plane/
// size. `size_mode` is "Auto" (a quarter of the circle's radius, recomputed
// from the live radius on every rebuild) or "Fixed" (the value the command
// was given via Size=, which never changes); either way `size` is the value
// actually drawn this time and gets stored back as the fallback CenterSize.
inline int BuildCentermarkGroup(CommandContext& ctx, Point3d center, const ON_Plane& pl, double size, int layer,
                                bool has_ref, ObjectId ref, const std::string& size_mode) {
  if (size <= 0) return -1;
  std::vector<kernel::NurbsCurve> curves = {PolylineCurve({center - pl.xaxis * size, center + pl.xaxis * size}),
                                            PolylineCurve({center - pl.yaxis * size, center + pl.yaxis * size})};
  std::map<std::string, std::string> tags;
  tags["CenterCenter"] = PointTag(center);
  tags["CenterPlaneOrigin"] = PointTag(pl.origin);
  tags["CenterPlaneX"] = PointTag(Point3d(pl.xaxis));
  tags["CenterPlaneY"] = PointTag(Point3d(pl.yaxis));
  tags["CenterSizeMode"] = size_mode;
  tags["CenterSize"] = FormatNumber(size);
  if (has_ref) tags["DimRefObj1"] = std::to_string(ref);
  return AddAnnotationGroup(ctx, "Centermark", curves, GlyphSpec{}, layer, tags);
}

// Resolves a Centermark group's center/plane/size to their *current* value:
// the referenced arc/circle's live center/plane/radius when DimRefObj1
// resolves via ResolveArcAnchor (Auto mode re-derives size as a quarter of
// the live radius; Fixed mode keeps the size the command was given), else
// the CenterCenter/CenterPlaneOrigin/X/Y/CenterSize recorded at creation.
inline bool ResolveCentermarkGeom(Document& doc, int group_id, Point3d& center, ON_Plane& pl, double& size) {
  std::string mode = "Fixed";
  double fixed_size = 0;
  bool has_ref = false;
  ObjectId ref = kNoObject;
  Point3d fb_center, fb_org, fb_ax, fb_ay;
  bool have_fb_center = false, have_fb_plane = false;
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    if (auto it = o.user_text.find("CenterSizeMode"); it != o.user_text.end()) mode = it->second;
    if (auto it = o.user_text.find("CenterSize"); it != o.user_text.end()) fixed_size = std::atof(it->second.c_str());
    if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) { ref = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); has_ref = true; }
    if (o.user_text.count("CenterCenter") && ParsePointTag(o.user_text.at("CenterCenter"), fb_center)) have_fb_center = true;
    if (o.user_text.count("CenterPlaneOrigin") && ParsePointTag(o.user_text.at("CenterPlaneOrigin"), fb_org) &&
        o.user_text.count("CenterPlaneX") && ParsePointTag(o.user_text.at("CenterPlaneX"), fb_ax) &&
        o.user_text.count("CenterPlaneY") && ParsePointTag(o.user_text.at("CenterPlaneY"), fb_ay)) have_fb_plane = true;
  }
  if (has_ref) {
    ON_Arc arc;
    if (ResolveArcAnchor(doc, ref, arc)) {
      center = arc.Center();
      pl = arc.plane;
      size = mode == "Auto" ? std::max(arc.Radius() * 0.25, 1e-9) : fixed_size;
      return size > 0;
    }
  }
  if (!have_fb_center) return false;
  center = fb_center;
  pl = have_fb_plane ? ON_Plane(fb_org, Vector3d(fb_ax.x, fb_ax.y, fb_ax.z), Vector3d(fb_ay.x, fb_ay.y, fb_ay.z)) : ON_Plane(fb_center, Vector3d(1, 0, 0), Vector3d(0, 1, 0));
  size = fixed_size;
  return size > 0;
}

// Builds (or rebuilds) one CenterLine group: a single line down the midline
// of two lines, pairing each line's near ends (rather than always start-to-
// start) so the midline follows however the two lines are actually wound -
// same approach as AutoCAD's CENTERLINE.
inline int BuildCenterLineGroup(CommandContext& ctx, Point3d m0, Point3d m1, int layer, ObjectId ref1, ObjectId ref2) {
  if (m0.DistanceTo(m1) < 1e-9) return -1;
  std::vector<kernel::NurbsCurve> curves = {PolylineCurve({m0, m1})};
  std::map<std::string, std::string> tags;
  tags["CenterP0"] = PointTag(m0);
  tags["CenterP1"] = PointTag(m1);
  tags["DimRefObj1"] = std::to_string(ref1);
  tags["DimRefObj2"] = std::to_string(ref2);
  return AddAnnotationGroup(ctx, "CenterLine", curves, GlyphSpec{}, layer, tags);
}

// Resolves a real curve object to a straight line, if it still is one -
// same "falls back to the static bake if the shape changed" contract as
// ResolveArcAnchor.
inline bool ResolveLineAnchor(Document& doc, ObjectId obj, ON_Line& out) {
  const SceneObject* o = doc.Find(obj);
  if (!o || o->kind != ObjectKind::Curve || !o->curve || !o->curve->raw().IsLinear()) return false;
  out = ON_Line(o->curve->raw().PointAtStart(), o->curve->raw().PointAtEnd());
  return true;
}

// Midline of two lines, pairing each line's nearer ends together.
inline void MidlineOf(const ON_Line& l1, const ON_Line& l2, Point3d& m0, Point3d& m1) {
  const double d0 = l1.from.DistanceTo(l2.from), d1 = l1.from.DistanceTo(l2.to);
  const Point3d near2 = d0 <= d1 ? l2.from : l2.to, far2 = d0 <= d1 ? l2.to : l2.from;
  m0 = Point3d((l1.from + near2) * 0.5);
  m1 = Point3d((l1.to + far2) * 0.5);
}

// Resolves a CenterLine group's midline endpoints to their *current* value:
// both referenced lines' live geometry when DimRefObj1/DimRefObj2 both still
// resolve to straight lines, else the CenterP0/CenterP1 recorded at creation.
inline bool ResolveCenterLinePoints(Document& doc, int group_id, Point3d& m0, Point3d& m1) {
  ObjectId ref1 = kNoObject, ref2 = kNoObject;
  bool has1 = false, has2 = false;
  Point3d fb0, fb1;
  bool have_fb = false;
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    if (auto it = o.user_text.find("DimRefObj1"); it != o.user_text.end()) { ref1 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); has1 = true; }
    if (auto it = o.user_text.find("DimRefObj2"); it != o.user_text.end()) { ref2 = static_cast<ObjectId>(std::strtoull(it->second.c_str(), nullptr, 10)); has2 = true; }
    if (o.user_text.count("CenterP0") && o.user_text.count("CenterP1") && ParsePointTag(o.user_text.at("CenterP0"), fb0) && ParsePointTag(o.user_text.at("CenterP1"), fb1)) have_fb = true;
  }
  if (has1 && has2) {
    ON_Line l1, l2;
    if (ResolveLineAnchor(doc, ref1, l1) && ResolveLineAnchor(doc, ref2, l2)) { MidlineOf(l1, l2, m0, m1); return true; }
  }
  if (!have_fb) return false;
  m0 = fb0; m1 = fb1;
  return true;
}

// Small "Name=Value" option reader for script-driven commands: consumes the
// pending tokens that look like options (keys lower-cased) and leaves the
// others queued for the command's prompts.
inline std::map<std::string, std::string> TakeOptionTokens(CommandContext& ctx) {
  std::map<std::string, std::string> out;
  std::vector<std::string> rest;
  while (std::optional<std::string> tok = ctx.Engine().TakePendingInput()) {
    const size_t eq = tok->find('=');
    if (eq == std::string::npos || eq == 0) { rest.push_back(*tok); continue; }
    out[ToLower(tok->substr(0, eq))] = tok->substr(eq + 1);
  }
  for (const std::string& t : rest) ctx.Engine().PendingInputs().push_back(t);
  return out;
}

inline std::string OptionOr(const std::map<std::string, std::string>& opts, const char* key, const std::string& def = "") {
  auto it = opts.find(key);
  return it == opts.end() ? def : it->second;
}

}  // namespace dino8::app
