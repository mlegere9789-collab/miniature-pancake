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
// text, as one group on the dimension layer. Returns the group id or -1.
inline int AddAnnotationGroup(CommandContext& ctx, const std::string& kind, const std::vector<kernel::NurbsCurve>& curves,
                              const GlyphSpec& text, int layer = -1) {
  if (layer < 0) layer = DimensionLayer(ctx);
  const std::string style = ctx.Settings().annotation_style;
  std::vector<ObjectId> ids;
  for (const kernel::NurbsCurve& c : curves) {
    SceneObject s = SceneObject::MakeCurve(c);
    s.layer_index = layer;
    TagAnnotation(s, kind, style);
    ids.push_back(ctx.Doc().Add(std::move(s)));
  }
  if (!text.text.empty()) {
    for (ObjectId id : AddGlyphCurves(ctx, text, layer, -1, {{"Annotation", kind}, {"Style", style}})) ids.push_back(id);
  }
  if (ids.empty()) return -1;
  return ctx.Doc().CreateGroup(ids, kind);
}

// Replaces the glyph curves of an annotation group with a rebuilt text.
// Returns the number of glyph curves made.
inline int RebuildGroupText(CommandContext& ctx, int group_id, const GlyphSpec& g) {
  std::vector<ObjectId> old;
  int layer = -1;
  std::map<std::string, std::string> tags;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (o.group_id != group_id || !o.user_text.count("Glyph")) continue;
    old.push_back(o.id);
    layer = o.layer_index;
    for (const char* k : {"Annotation", "Style"}) { auto it = o.user_text.find(k); if (it != o.user_text.end()) tags[k] = it->second; }
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
