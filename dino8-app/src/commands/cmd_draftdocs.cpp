// Drafting and documentation commands from the AutoCAD column of the Parity Register (factory drafting-docs):
//   TCount            number selected texts in X / Y / selection order, with prefix, suffix, overwrite or find-and-replace
//   TextAlign         align texts to a base text (Left / Center / Right / Top / Middle / Bottom)
//   Txt2Mtxt          merge or convert single-line texts into MTEXT (multi-line), sorted top-down or in selection order
//   HatchToBack       send every hatch behind every other object (draw order)
//   SpaceTrans        convert a length between model space and paper space with a layout detail's scale
//   DimContinue       chain linear dimensions from the second extension line of the base dimension
//   DimBaseline       stack linear dimensions on the first extension line of the base dimension
//   DimSpace          equalize the spacing of parallel linear dimensions
//   DimArc            arc-length dimension (whole arc or between two picked points)
//   DimDisassociate   strip associativity from dimensions (they keep their current geometry)
// Annotations in this app are groups of baked curves tagged with user text (commands/annotate_common.h, DimGeometry.h); every command
// here reads and writes those tags and the document's own groups, so DXF export, UpdateDimensions and the selection commands
// treat the results exactly like the output of Text / Dim / DimAligned.
#include "commands/annotate_common.h"
#include "commands/cmd_common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace dino8::app {

namespace {

std::string Lc(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool IsYes(const std::string& v) {
  const std::string l = Lc(v);
  return l == "yes" || l == "true" || l == "1" || l == "on";
}


void DdLine(std::vector<kernel::NurbsCurve>& out, Point3d a, Point3d b) { out.push_back(PolylineCurve({a, b})); }

// The same open arrowhead the Dim* commands draw: tip, `size` back along -dir, 0.3 size to each side.
void DdArrow(std::vector<kernel::NurbsCurve>& out, Point3d tip, Vector3d dir, double size, const ON_Plane& pl) {
  dir.Unitize();
  Vector3d side = ON_CrossProduct(pl.zaxis, dir);
  side.Unitize();
  const Point3d a = tip - dir * size + side * (size * 0.3), b = tip - dir * size - side * (size * 0.3);
  out.push_back(PolylineCurve({a, tip, b, a}));
}

// A whole number prints without a decimal point at any size ("100000", not "1e+05"); anything else uses FormatNumber.
std::string CountText(double n) {
  if (std::fabs(n - std::round(n)) < 1e-9 && std::fabs(n) < 1e15) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(std::llround(n)));
    return buf;
  }
  return FormatNumber(n);
}

std::string GroupAnnotationKind(Document& doc, int group_id) {
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    auto it = o.user_text.find("Annotation");
    if (it != o.user_text.end()) return it->second;
  }
  return "";
}

// One single-line Text group (the Text command's output): its glyph spec and its layout width.
struct TextItem {
  int group = -1;
  GlyphSpec spec;
  double width = 0;   // advance width of the whole string at spec.height
  int order = 0;      // position in the selection
};

// The distinct single-line Text groups among `ids`, in the order they were selected.
std::vector<TextItem> TextItemsOf(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<TextItem> out;
  for (int g : AnnotationGroupsOf(ctx, ids)) {
    if (GroupAnnotationKind(ctx.Doc(), g) != "Text") continue;
    TextItem t;
    t.group = g;
    if (!GroupGlyphSpec(ctx, g, t.spec)) continue;
    std::vector<kernel::NurbsCurve> glyphs;
    std::string font;
    double adv = 0;
    ON_Plane pl = t.spec.plane;
    if (TextToCurves(t.spec.text, t.spec.height, pl, glyphs, font, &adv)) t.width = adv;
    t.order = static_cast<int>(out.size());
    out.push_back(t);
  }
  return out;
}

// ---------------------------------------------------------------------------
// TCount
// ---------------------------------------------------------------------------

class TCountCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto o = TakeOptionTokens(ctx);
    start_ = std::atof(OptionOr(o, "start", "1").c_str());
    inc_ = std::atof(OptionOr(o, "increment", "1").c_str());
    order_ = Lc(OptionOr(o, "order", "selection"));
    mode_ = Lc(OptionOr(o, "mode", "overwrite"));
    prefix_ = OptionOr(o, "prefix");
    suffix_ = OptionOr(o, "suffix");
    find_ = OptionOr(o, "find");
    options = {{"Start", FormatNumber(start_), {}, true, false},
               {"Increment", FormatNumber(inc_), {}, true, false},
               {"Order", order_, {"Selection", "X", "Y"}, false, false},
               {"Mode", mode_, {"Overwrite", "Prefix", "Suffix", "Replace"}, false, false},
               {"Prefix", prefix_, {}, false, false},
               {"Suffix", suffix_, {}, false, false},
               {"Find", find_, {}, false, false}};
    WantObjects("Select text to number");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    const std::string k = Lc(n);
    if (k == "start") { start_ = std::atof(v.c_str()); options[0].value = FormatNumber(start_); }
    else if (k == "increment") { inc_ = std::atof(v.c_str()); options[1].value = FormatNumber(inc_); }
    else if (k == "order") { order_ = Lc(v); options[2].value = v; }
    else if (k == "mode") { mode_ = Lc(v); options[3].value = v; }
    else if (k == "prefix") { prefix_ = v; options[4].value = v; }
    else if (k == "suffix") { suffix_ = v; options[5].value = v; }
    else if (k == "find") { find_ = v; options[6].value = v; }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    std::vector<TextItem> items = TextItemsOf(ctx, ids);
    if (items.empty()) { ctx.Warn("TCount: select single-line text (the Text command's output)"); Finish(); return; }
    if (mode_ != "overwrite" && mode_ != "prefix" && mode_ != "suffix" && mode_ != "replace") { ctx.Warn("TCount: Mode must be Overwrite, Prefix, Suffix or Replace"); Finish(); return; }
    if (mode_ == "replace" && find_.empty()) { ctx.Warn("TCount: Mode=Replace needs Find=<text to replace>"); Finish(); return; }
    // Ascending along X or Y (ties keep the selection order); the default is the order of selection.
    if (order_ == "x") std::stable_sort(items.begin(), items.end(), [](const TextItem& a, const TextItem& b) { return a.spec.plane.origin.x < b.spec.plane.origin.x; });
    else if (order_ == "y") std::stable_sort(items.begin(), items.end(), [](const TextItem& a, const TextItem& b) { return a.spec.plane.origin.y < b.spec.plane.origin.y; });
    ctx.Doc().BeginChange("TCount");
    int numbered = 0;
    double n = start_;
    for (TextItem& t : items) {
      const std::string label = prefix_ + CountText(n) + suffix_;
      std::string text = t.spec.text;
      if (mode_ == "overwrite") text = label;
      else if (mode_ == "prefix") text = label + text;
      else if (mode_ == "suffix") text = text + label;
      else {  // Replace: every occurrence of Find; texts without it keep their text and do not use a number
        if (text.find(find_) == std::string::npos) continue;
        std::string out;
        size_t pos = 0, hit;
        while ((hit = text.find(find_, pos)) != std::string::npos) { out += text.substr(pos, hit - pos) + label; pos = hit + find_.size(); }
        out += text.substr(pos);
        text = out;
      }
      GlyphSpec spec = t.spec;
      spec.text = text;
      if (RebuildGroupText(ctx, t.group, spec) > 0) {
        ++numbered;
        n += inc_;
      }
    }
    ctx.Print("TCount: " + std::to_string(numbered) + " text(s) numbered from " + CountText(start_) + " by " + CountText(inc_) + " (order " + order_ + ", " + mode_ + ")");
    Finish();
  }
  double start_ = 1, inc_ = 1;
  std::string order_, mode_, prefix_, suffix_, find_;
};

// ---------------------------------------------------------------------------
// TextAlign
// ---------------------------------------------------------------------------

// Edges of a text's layout box measured in a frame (origin o, axes u and v): the box is [left, left + width] x [bottom, bottom + height],
// anchored at the glyph spec's origin (the centre of the width for a centred text).
struct TextEdges { double left, right, hcenter, bottom, top, vcenter; };
TextEdges EdgesIn(const TextItem& t, const Point3d& o, const Vector3d& u, const Vector3d& v) {
  const Vector3d d = t.spec.plane.origin - o;
  TextEdges e;
  e.left = ON_DotProduct(d, u) - (t.spec.center ? t.width / 2.0 : 0.0);
  e.right = e.left + t.width;
  e.hcenter = e.left + t.width / 2.0;
  e.bottom = ON_DotProduct(d, v);
  e.top = e.bottom + t.spec.height;
  e.vcenter = e.bottom + t.spec.height / 2.0;
  return e;
}

class TextAlignCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto o = TakeOptionTokens(ctx);
    mode_ = Lc(OptionOr(o, "mode", "left"));
    first_ = Lc(OptionOr(o, "base")) == "first";
    options = {{"Mode", mode_, {"Left", "Center", "Right", "Top", "Middle", "Bottom"}, false, false}, {"Base", first_ ? "First" : "Pick", {"Pick", "First"}, false, false}};
    WantObjects("Select text to align");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    const std::string k = Lc(n);
    if (k == "mode") { mode_ = Lc(v); options[0].value = v; }
    if (k == "base") { first_ = Lc(v) == "first"; options[1].value = v; }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    static const char* kModes[] = {"left", "center", "right", "top", "middle", "bottom"};
    if (std::find(std::begin(kModes), std::end(kModes), mode_) == std::end(kModes)) { ctx.Warn("TextAlign: Mode must be Left, Center, Right, Top, Middle or Bottom"); Finish(); return; }
    if (!have_texts_) {
      texts_ = TextItemsOf(ctx, ids);
      if (texts_.empty()) { ctx.Warn("TextAlign: select single-line text (the Text command's output)"); Finish(); return; }
      have_texts_ = true;
      if (first_) { Apply(ctx, texts_.front().group); return; }
      ctx.Doc().SelectNone();
      WantObjects("Select the base text");
      return;
    }
    const std::vector<int> groups = AnnotationGroupsOf(ctx, ids);
    int base = -1;
    for (int g : groups) if (GroupAnnotationKind(ctx.Doc(), g) == "Text") { base = g; break; }
    if (base < 0) { ctx.Warn("TextAlign: the base must be a single-line text"); Finish(); return; }
    Apply(ctx, base);
  }
  void Apply(CommandContext& ctx, int base_group) {
    std::vector<ObjectId> member_ids;
    for (const SceneObject& o : ctx.Doc().Objects()) if (o.group_id == base_group) member_ids.push_back(o.id);
    std::vector<TextItem> base_items = TextItemsOf(ctx, member_ids);
    if (base_items.empty()) { ctx.Warn("TextAlign: the base text could not be read"); Finish(); return; }
    const TextItem& base = base_items.front();
    const Point3d o = base.spec.plane.origin;
    const Vector3d u = base.spec.plane.xaxis, v = base.spec.plane.yaxis;
    const TextEdges be = EdgesIn(base, o, u, v);
    ctx.Doc().BeginChange("TextAlign");
    int moved = 0;
    for (TextItem& t : texts_) {
      if (t.group == base_group) continue;
      const TextEdges te = EdgesIn(t, o, u, v);
      double du = 0, dv = 0;
      if (mode_ == "left") du = be.left - te.left;
      else if (mode_ == "right") du = be.right - te.right;
      else if (mode_ == "center") du = be.hcenter - te.hcenter;
      else if (mode_ == "top") dv = be.top - te.top;
      else if (mode_ == "middle") dv = be.vcenter - te.vcenter;
      else dv = be.bottom - te.bottom;
      if (std::fabs(du) < 1e-12 && std::fabs(dv) < 1e-12) continue;
      GlyphSpec spec = t.spec;
      spec.plane.SetOrigin(spec.plane.origin + u * du + v * dv);
      if (RebuildGroupText(ctx, t.group, spec) > 0) ++moved;
    }
    ctx.Print("TextAlign: " + std::to_string(moved) + " text(s) aligned " + mode_ + " to the base text");
    Finish();
  }
  std::string mode_ = "left";
  bool first_ = false, have_texts_ = false;
  std::vector<TextItem> texts_;
};

// ---------------------------------------------------------------------------
// Txt2Mtxt
// ---------------------------------------------------------------------------

constexpr double kMTextLineSpacing = 1.5;  // line pitch / text height, the factor the DXF MTEXT reader lays lines out with

// Replaces the single-line texts `items` (in this order) by ONE multi-line text object: line i has its baseline i * 1.5 * height below
// the first, the top-left corner is where the first line's cap height ends. Tags it like an imported MTEXT (MText*, Text with '\n') so
// ExportDxf writes one MTEXT entity. Returns false if no outline font was found.
bool MakeMText(CommandContext& ctx, const std::vector<TextItem>& items) {
  const TextItem& first = items.front();
  const double h = first.spec.height;
  const Vector3d ux = first.spec.plane.xaxis, uy = first.spec.plane.yaxis;
  const Point3d left_origin = first.spec.plane.origin - ux * (first.spec.center ? first.width / 2.0 : 0.0);
  const Point3d top_left = left_origin + uy * h;
  const ON_Plane base(top_left, ux, uy);
  int layer = ctx.Doc().CurrentLayer();
  std::string style = ctx.Settings().annotation_style;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (o.group_id != first.group || !o.user_text.count("Glyph")) continue;
    layer = o.layer_index;
    auto it = o.user_text.find("Style");
    if (it != o.user_text.end()) style = it->second;
    break;
  }
  std::string joined;
  for (size_t i = 0; i < items.size(); ++i) { if (i) joined += "\n"; joined += items[i].spec.text; }
  std::vector<std::vector<kernel::NurbsCurve>> per_line(items.size());
  bool any = false;
  for (size_t i = 0; i < items.size(); ++i) {
    const double baseline_y = -h - static_cast<double>(i) * h * kMTextLineSpacing;
    const ON_Plane pl(base.PointAt(0.0, baseline_y), ux, uy);
    std::string font;
    double adv = 0;
    if (items[i].spec.text.empty()) continue;
    if (TextToCurves(items[i].spec.text, h, pl, per_line[i], font, &adv) && !per_line[i].empty()) any = true;
  }
  if (!any) return false;
  // Remove the source texts first, then add the merged one.
  std::vector<ObjectId> doomed;
  for (const TextItem& t : items)
    for (const SceneObject& o : ctx.Doc().Objects()) if (o.group_id == t.group) doomed.push_back(o.id);
  for (ObjectId id : doomed) ctx.Doc().Remove(id);
  std::vector<ObjectId> new_ids;
  for (std::vector<kernel::NurbsCurve>& line : per_line) {
    for (kernel::NurbsCurve& c : line) {
      SceneObject s = SceneObject::MakeCurve(c);
      s.layer_index = layer;
      TagAnnotation(s, "Text", style);
      s.user_text["Text"] = joined;
      s.user_text["MText"] = "1";
      s.user_text["MTextHeight"] = FormatNumber(h);
      s.user_text["MTextOrigin"] = PointTag(base.origin);
      s.user_text["MTextX"] = PointTag(Point3d(ux));
      s.user_text["MTextY"] = PointTag(Point3d(uy));
      s.user_text["MTextAttach"] = "1";
      s.user_text["MTextWidth"] = "0";
      new_ids.push_back(ctx.Doc().Add(std::move(s)));
    }
  }
  ctx.Doc().CreateGroup(new_ids, "Text");
  return true;
}

class Txt2MtxtCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto o = TakeOptionTokens(ctx);
    combine_ = IsYes(OptionOr(o, "combine", "Yes"));
    order_ = Lc(OptionOr(o, "order", "topdown"));
    options = {{"Combine", combine_ ? "Yes" : "No", {"Yes", "No"}, false, true}, {"Order", order_, {"TopDown", "Selection"}, false, false}};
    WantObjects("Select text to convert to multiline text");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    const std::string k = Lc(n);
    if (k == "combine") { combine_ = IsYes(v); options[0].value = combine_ ? "Yes" : "No"; }
    if (k == "order") { order_ = Lc(v); options[1].value = v; }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    std::vector<TextItem> items = TextItemsOf(ctx, ids);
    if (items.empty()) { ctx.Warn("Txt2Mtxt: select single-line text (the Text command's output)"); Finish(); return; }
    if (order_ != "selection") {
      // Top to bottom (larger Y first); texts on the same baseline go left to right.
      std::stable_sort(items.begin(), items.end(), [](const TextItem& a, const TextItem& b) {
        if (std::fabs(a.spec.plane.origin.y - b.spec.plane.origin.y) > 1e-9) return a.spec.plane.origin.y > b.spec.plane.origin.y;
        return a.spec.plane.origin.x < b.spec.plane.origin.x;
      });
    }
    ctx.Doc().BeginChange("Txt2Mtxt");
    int made = 0;
    bool ok = true;
    if (combine_) {
      ok = MakeMText(ctx, items);
      made = ok ? 1 : 0;
    } else {
      for (const TextItem& t : items) {
        if (!MakeMText(ctx, {t})) { ok = false; break; }
        ++made;
      }
    }
    if (!ok) ctx.Warn("Txt2Mtxt: no TrueType font found for text outlines");
    ctx.Print("Txt2Mtxt: " + std::to_string(items.size()) + " text(s) -> " + std::to_string(made) + " multiline text object(s)");
    Finish();
  }
  bool combine_ = true;
  std::string order_ = "topdown";
};

// ---------------------------------------------------------------------------
// HatchToBack
// ---------------------------------------------------------------------------

void DoHatchToBack(CommandContext& ctx) {
  Document& doc = ctx.Doc();
  int lo = 0;  // objects without a DrawOrder are at 0
  size_t hatches = 0, others = 0;
  for (const SceneObject& o : doc.Objects()) {
    if (o.user_text.count("Hatch")) { ++hatches; continue; }
    ++others;
    auto it = o.user_text.find("DrawOrder");
    if (it != o.user_text.end()) lo = std::min(lo, std::atoi(it->second.c_str()));
  }
  if (hatches == 0) { ctx.Print("HatchToBack: the drawing has no hatches"); return; }
  doc.BeginChange("HatchToBack");
  const std::string back = std::to_string(lo - 1);
  for (SceneObject& o : doc.Objects()) if (o.user_text.count("Hatch")) o.user_text["DrawOrder"] = back;  // locked layers included
  doc.Touch();
  ctx.Print("HatchToBack: " + std::to_string(hatches) + " hatch object(s) sent behind " + std::to_string(others) + " other object(s) (draw order " + back + ")");
}

// ---------------------------------------------------------------------------
// SpaceTrans
// ---------------------------------------------------------------------------

class SpaceTransCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto o = TakeOptionTokens(ctx);
    to_ = Lc(OptionOr(o, "to", "model"));
    detail_ = OptionOr(o, "detail");
    options = {{"To", to_, {"Model", "Paper"}, false, false}, {"Detail", detail_, {}, false, false}};
    WantNumber("Length to convert");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    const std::string k = Lc(n);
    if (k == "to") { to_ = Lc(v); options[0].value = v; }
    if (k == "detail") { detail_ = v; options[1].value = v; }
  }
  void OnNumber(CommandContext& ctx, double len) override {
    Layout* L = ctx.App().ActiveLayout();
    if (!L) { ctx.Warn("SpaceTrans: switch to a layout first (Layout / Layouts)"); Finish(); return; }
    if (to_ != "model" && to_ != "paper") { ctx.Warn("SpaceTrans: To must be Model or Paper"); Finish(); return; }
    const LayoutDetail* d = nullptr;
    if (!detail_.empty()) {
      for (const LayoutDetail& x : L->details) if (Lc(x.name) == Lc(detail_)) { d = &x; break; }
      if (!d) { ctx.Warn("SpaceTrans: the active layout has no detail named " + detail_); Finish(); return; }
    } else {
      for (const LayoutDetail& x : L->details) if (x.selected) { d = &x; break; }
      if (!d && L->details.size() == 1) d = &L->details.front();
      if (!d) { ctx.Warn("SpaceTrans: select a detail first or name one with Detail=<name>"); Finish(); return; }
    }
    if (!(d->scale > 0)) { ctx.Warn("SpaceTrans: detail " + d->name + " has no scale (free zoom); give it a Scale first"); Finish(); return; }
    if (!(len > 0)) { ctx.Warn("SpaceTrans: the length must be positive"); Finish(); return; }
    // LayoutDetail::scale is page millimetres per model unit.
    const double out = to_ == "model" ? len / d->scale : len * d->scale;
    ctx.Print(std::string("SpaceTrans: ") + FormatNumber(len) + (to_ == "model" ? " paper mm = " : " model units = ") + FormatNumber(out) +
              (to_ == "model" ? " model units" : " paper mm") + " (detail " + d->name + ", scale " + FormatNumber(d->scale) + ")");
    Finish();
  }
  std::string to_ = "model", detail_;
};

// ---------------------------------------------------------------------------
// Linear dimension helpers (DimContinue / DimBaseline / DimSpace)
// ---------------------------------------------------------------------------

struct LinDim {
  int group = -1;
  LinearDimLayout layout;
  Point3d p0, p1;
  double text_h = 1;
  std::string style;
  std::string ref[4];  // DimRefObj1, DimRefEnd1, DimRefObj2, DimRefEnd2 (empty when not anchored)
};

// Reads a DimLinear / DimAligned group back from its tags.
bool ReadLinDim(CommandContext& ctx, int group, LinDim& d) {
  bool have_layout = false, have_pts = false;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (o.group_id != group) continue;
    const auto& ut = o.user_text;
    if (!have_layout && ut.count("DimPlaneOrigin") && ut.count("DimPlaneX") && ut.count("DimPlaneY") && ut.count("DimOffset")) {
      Point3d org, ax, ay;
      if (ParsePointTag(ut.at("DimPlaneOrigin"), org) && ParsePointTag(ut.at("DimPlaneX"), ax) && ParsePointTag(ut.at("DimPlaneY"), ay)) {
        d.layout.plane = ON_Plane(org, Vector3d(ax.x, ax.y, ax.z), Vector3d(ay.x, ay.y, ay.z));
        d.layout.aligned = ut.count("DimAligned") && ut.at("DimAligned") == "1";
        d.layout.horizontal = !ut.count("DimHorizontal") || ut.at("DimHorizontal") == "1";
        d.layout.offset = std::atof(ut.at("DimOffset").c_str());
        have_layout = true;
      }
    }
    if (!have_pts && ut.count("DimP0") && ut.count("DimP1") && ParsePointTag(ut.at("DimP0"), d.p0) && ParsePointTag(ut.at("DimP1"), d.p1)) have_pts = true;
    const char* keys[4] = {"DimRefObj1", "DimRefEnd1", "DimRefObj2", "DimRefEnd2"};
    for (int i = 0; i < 4; ++i) if (d.ref[i].empty() && ut.count(keys[i])) d.ref[i] = ut.at(keys[i]);
    auto st = ut.find("Style");
    if (st != ut.end() && d.style.empty()) d.style = st->second;
  }
  if (!have_layout || !have_pts) return false;
  GlyphSpec g;
  if (GroupGlyphSpec(ctx, group, g) && g.height > 0) d.text_h = g.height;
  d.group = group;
  return true;
}

// Builds one linear dimension group; `ref1`/`ref2` are the anchor tags ({obj, end}) of the two measured points ("" = none).
int AddLinDim(CommandContext& ctx, const LinDim& d, const std::string* ref1, const std::string* ref2, double* len_out) {
  std::vector<kernel::NurbsCurve> curves;
  DimGlyphSpec dg;
  std::map<std::string, std::string> tags;
  const DimStyleParams style = d.style.empty() ? StyleParamsOf(ctx) : StyleParamsByName(ctx.Doc(), d.style);
  if (!BuildLinearDimensionGeometry(d.p0, d.p1, d.layout, d.text_h, curves, dg, tags, len_out, style)) return -1;
  if (ref1 && !ref1[0].empty()) { tags["DimRefObj1"] = ref1[0]; tags["DimRefEnd1"] = ref1[1].empty() ? "point" : ref1[1]; }
  if (ref2 && !ref2[0].empty()) { tags["DimRefObj2"] = ref2[0]; tags["DimRefEnd2"] = ref2[1].empty() ? "point" : ref2[1]; }
  GlyphSpec g;
  g.text = dg.text; g.height = dg.height; g.plane = dg.plane; g.center = dg.center;
  return AddAnnotationGroup(ctx, d.layout.aligned ? "DimAligned" : "DimLinear", curves, g, -1, tags);
}

// The first linear dimension group among `ids`.
bool BaseLinDim(CommandContext& ctx, const std::vector<ObjectId>& ids, LinDim& d) {
  for (int g : AnnotationGroupsOf(ctx, ids)) {
    const std::string k = GroupAnnotationKind(ctx.Doc(), g);
    if ((k == "DimLinear" || k == "DimAligned") && ReadLinDim(ctx, g, d)) return true;
  }
  return false;
}

// Which way is "away from the measured points": +1 / -1 along the offset coordinate.
double OutwardSign(const LinDim& d) {
  if (d.layout.aligned) return d.layout.offset >= 0 ? 1.0 : -1.0;
  double ua, va, ub, vb;
  d.layout.plane.ClosestPointTo(d.p0, &ua, &va);
  d.layout.plane.ClosestPointTo(d.p1, &ub, &vb);
  const double mid = d.layout.horizontal ? (va + vb) / 2.0 : (ua + ub) / 2.0;
  return d.layout.offset >= mid ? 1.0 : -1.0;
}

// DimContinue (continue = true) and DimBaseline (continue = false).
class DimChainCommand : public Command {
 public:
  explicit DimChainCommand(bool continued) : continued_(continued) {}
  void Begin(CommandContext& ctx) override {
    auto o = TakeOptionTokens(ctx);
    spacing_ = std::atof(OptionOr(o, "spacing", "3.75").c_str());  // AutoCAD's DIMDLI default
    options = {{"Spacing", FormatNumber(spacing_), {}, true, false}};
    WantObjects(continued_ ? "Select the dimension to continue from" : "Select the base dimension");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (Lc(n) == "spacing") { spacing_ = std::atof(v.c_str()); options[0].value = FormatNumber(spacing_); }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!BaseLinDim(ctx, ids, base_)) { ctx.Warn(std::string(continued_ ? "DimContinue" : "DimBaseline") + ": select a linear or aligned dimension"); Finish(); return; }
    last_ = base_;
    ctx.Doc().SelectNone();
    WantPoint(continued_ ? "Second extension line origin (Enter to finish)" : "Second extension line origin (Enter to finish)");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    LinDim d = last_;
    std::string ref1[2], ref2[2];
    ObjectId obj = kNoObject;
    std::string end;
    const bool anchored = FindPointAnchor(ctx.Doc(), p, obj, end);
    if (anchored) { ref2[0] = std::to_string(obj); ref2[1] = end; }
    if (continued_) {
      // Starts at the second extension line of the previous dimension.
      d.p0 = last_.p1;
      ref1[0] = last_.ref[2]; ref1[1] = last_.ref[3];
    } else {
      // Shares the FIRST extension line of the base dimension; each new dimension line sits one spacing beyond the previous one.
      d.p0 = base_.p0;
      ref1[0] = base_.ref[0]; ref1[1] = base_.ref[1];
      d.layout.offset = last_.layout.offset + OutwardSign(base_) * spacing_;
    }
    d.p1 = p;
    ctx.Doc().BeginChange(continued_ ? "DimContinue" : "DimBaseline");
    double len = 0;
    const int g = AddLinDim(ctx, d, ref1, ref2, &len);
    if (g < 0) { ctx.Warn("The new point coincides with the dimension's start"); return; }
    ++made_;
    ctx.Print(std::string(continued_ ? "DimContinue " : "DimBaseline ") + FormatNumber(len));
    // The new dimension becomes the one the next point continues from.
    d.group = g;
    d.ref[0] = ref1[0]; d.ref[1] = ref1[1]; d.ref[2] = ref2[0]; d.ref[3] = ref2[1];
    last_ = d;
  }
  void OnEnter(CommandContext& ctx) override {
    ctx.Print(std::string(continued_ ? "DimContinue: " : "DimBaseline: ") + std::to_string(made_) + " dimension(s) added");
    Finish();
  }
  bool continued_;
  double spacing_ = 3.75;
  int made_ = 0;
  LinDim base_, last_;
};

// DimSpace: base dimension first, then the dimensions to space.
class DimSpaceCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto o = TakeOptionTokens(ctx);
    const std::string sp = OptionOr(o, "spacing", "auto");
    autospace_ = Lc(sp) == "auto";
    spacing_ = autospace_ ? 0 : std::atof(sp.c_str());
    options = {{"Spacing", sp, {}, true, false}};
    WantObjects("Select the base dimension");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (Lc(n) != "spacing") return;
    autospace_ = Lc(v) == "auto";
    spacing_ = autospace_ ? 0 : std::atof(v.c_str());
    options[0].value = v;
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!have_base_) {
      if (!BaseLinDim(ctx, ids, base_)) { ctx.Warn("DimSpace: select a linear or aligned dimension as the base"); Finish(); return; }
      have_base_ = true;
      ctx.Doc().SelectNone();
      WantObjects("Select the dimensions to space");
      return;
    }
    if (spacing_ < 0) { ctx.Warn("DimSpace: the spacing cannot be negative"); Finish(); return; }
    const double step = autospace_ ? 2.0 * base_.text_h : spacing_;  // Auto = twice the text height
    // The stacking axis: a unit vector, and each dimension line's position along it.
    Vector3d axis;
    if (base_.layout.aligned) {
      axis = ON_CrossProduct(base_.layout.plane.zaxis, Vector3d(base_.p1 - base_.p0));
      axis.Unitize();
    } else {
      axis = base_.layout.horizontal ? base_.layout.plane.yaxis : base_.layout.plane.xaxis;
    }
    auto position = [&](const LinDim& d, double& pos, double& sgn) -> bool {
      if (d.layout.aligned != base_.layout.aligned) return false;
      if (!d.layout.aligned) {
        if (d.layout.horizontal != base_.layout.horizontal) return false;
        pos = d.layout.offset;
        sgn = 1.0;
        return true;
      }
      Vector3d n = ON_CrossProduct(d.layout.plane.zaxis, Vector3d(d.p1 - d.p0));
      if (!n.Unitize()) return false;
      const double c = ON_DotProduct(n, axis);
      if (std::fabs(std::fabs(c) - 1.0) > 1e-6) return false;  // not parallel to the base
      sgn = c > 0 ? 1.0 : -1.0;
      pos = ON_DotProduct(Vector3d(d.p0), axis) + d.layout.offset * sgn;
      return true;
    };
    double base_pos = 0, base_sgn = 1;
    position(base_, base_pos, base_sgn);
    struct Item { LinDim d; double pos, sgn, rel; };
    std::vector<Item> items;
    for (int g : AnnotationGroupsOf(ctx, ids)) {
      if (g == base_.group) continue;
      const std::string k = GroupAnnotationKind(ctx.Doc(), g);
      if (k != "DimLinear" && k != "DimAligned") continue;
      Item it;
      if (!ReadLinDim(ctx, g, it.d) || !position(it.d, it.pos, it.sgn)) continue;
      it.rel = it.pos - base_pos;
      items.push_back(it);
    }
    if (items.empty()) { ctx.Warn("DimSpace: no parallel linear dimensions to space"); Finish(); return; }
    // Dimensions keep their order on each side of the base: nearest first, then one step further for each.
    std::vector<Item*> pos_side, neg_side;
    for (Item& it : items) (it.rel >= 0 ? pos_side : neg_side).push_back(&it);
    std::stable_sort(pos_side.begin(), pos_side.end(), [](const Item* a, const Item* b) { return a->rel < b->rel; });
    std::stable_sort(neg_side.begin(), neg_side.end(), [](const Item* a, const Item* b) { return a->rel > b->rel; });
    ctx.Doc().BeginChange("DimSpace");
    int done = 0;
    auto rebuild = [&](Item& it, double new_pos) {
      LinDim d = it.d;
      d.layout.offset = d.layout.aligned ? it.sgn * (new_pos - ON_DotProduct(Vector3d(d.p0), axis)) : new_pos;
      GlyphSpec old_glyph;
      std::string tol;
      if (GroupGlyphSpec(ctx, it.d.group, old_glyph)) tol = GroupToleranceSuffix(ctx, it.d.group, old_glyph);
      std::vector<ObjectId> doomed;
      for (const SceneObject& o : ctx.Doc().Objects()) if (o.group_id == it.d.group) doomed.push_back(o.id);
      std::string refs1[2] = {it.d.ref[0], it.d.ref[1]}, refs2[2] = {it.d.ref[2], it.d.ref[3]};
      const int g = AddLinDim(ctx, d, refs1, refs2, nullptr);
      if (g < 0) return;
      for (ObjectId id : doomed) ctx.Doc().Remove(id);
      ReapplyToleranceSuffix(ctx, g, tol);
      ++done;
    };
    for (size_t i = 0; i < pos_side.size(); ++i) rebuild(*pos_side[i], base_pos + step * static_cast<double>(i + 1));
    for (size_t i = 0; i < neg_side.size(); ++i) rebuild(*neg_side[i], base_pos - step * static_cast<double>(i + 1));
    ctx.Print("DimSpace: " + std::to_string(done) + " dimension(s) spaced " + FormatNumber(step) + " apart" + (step == 0 ? " (aligned with the base)" : ""));
    Finish();
  }
  bool have_base_ = false, autospace_ = true;
  double spacing_ = 0;
  LinDim base_;
};

// ---------------------------------------------------------------------------
// DimArc
// ---------------------------------------------------------------------------

class DimArcCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto o = TakeOptionTokens(ctx);
    partial_ = IsYes(OptionOr(o, "partial", "No"));
    options = {{"Partial", partial_ ? "Yes" : "No", {"Yes", "No"}, false, true}};
    WantObjects("Select the arc to dimension");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (Lc(n) == "partial") { partial_ = IsYes(v); options[0].value = partial_ ? "Yes" : "No"; }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      ON_Arc arc;
      if (o && o->kind == ObjectKind::Curve && o->curve && !o->user_text.count("Annotation") && o->curve->raw().IsArc(nullptr, &arc, ctx.Settings().absolute_tolerance) && !arc.IsCircle()) { arc_ = arc; have_ = true; break; }
    }
    if (!have_) { ctx.Warn("DimArc: select an arc"); Finish(); return; }
    if (partial_) WantPoint("First point on the arc");
    else WantPoint("Dimension arc location");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (partial_ && pts_.size() < 2) {
      pts_.push_back(p);
      if (pts_.size() == 1) WantPoint("Second point on the arc");
      else WantPoint("Dimension arc location");
      return;
    }
    Build(ctx, p);
  }
  // Angle (radians, from the arc's x axis, counterclockwise) of point p about the arc centre.
  double AngleOf(const Point3d& p) const {
    double u, v;
    arc_.Plane().ClosestPointTo(p, &u, &v);
    return std::atan2(v, u);
  }
  void Build(CommandContext& ctx, Point3d loc) {
    const ON_Plane& pl = arc_.Plane();
    const Point3d c = arc_.Center();
    const double r = arc_.Radius();
    double a0 = arc_.Domain()[0], a1 = arc_.Domain()[1];
    if (partial_) {
      // The two picked points, as angles inside the arc's own sweep.
      double t0 = AngleOf(pts_[0]), t1 = AngleOf(pts_[1]);
      auto inside = [&](double t) { double x = t; while (x < a0 - 1e-9) x += 2 * ON_PI; return x <= a1 + 1e-9 ? x : t; };
      t0 = inside(t0); t1 = inside(t1);
      if (t0 > t1) std::swap(t0, t1);
      a0 = std::max(a0, t0); a1 = std::min(a1, t1);
    }
    const double sweep = a1 - a0;
    if (!(sweep > 1e-9)) { ctx.Warn("DimArc: the two points give no arc length"); Finish(); return; }
    const double h = AnnotationTextHeight(ctx);
    const double len = r * sweep;
    double u, v;
    pl.ClosestPointTo(loc, &u, &v);
    double R = std::hypot(u, v);
    if (R <= r + 1e-9) R = r + 2.0 * h;  // a location on or inside the arc: put the dimension arc two text heights outside it
    const double amid = (a0 + a1) / 2.0;
    auto at = [&](double rad, double ang) { return pl.PointAt(rad * std::cos(ang), rad * std::sin(ang)); };
    std::vector<kernel::NurbsCurve> curves;
    // Dimension arc, from a0 to a1 at radius R.
    {
      ON_Arc dim_arc(ON_Circle(pl, R), ON_Interval(a0, a1));
      kernel::NurbsCurve nc;
      if (CurveFromON(ON_ArcCurve(dim_arc), nc)) curves.push_back(nc);
    }
    // Radial extension lines from the measured arc out past the dimension arc, and the arrowheads.
    const double gap = std::min(0.25 * h, 0.5 * (R - r));
    for (double ang : {a0, a1}) {
      DdLine(curves, at(r + gap, ang), at(R + 0.5 * h, ang));
      const Vector3d tangent = (ang == a0 ? 1.0 : -1.0) * (at(R, ang + 1e-3) - at(R, ang - 1e-3));  // pointing along the dimension arc, inward
      DdArrow(curves, at(R, ang), -tangent, h, pl);
    }
    // The arc symbol: a short arc drawn above the text.
    const Point3d text_pt = at(R + 0.6 * h, amid);
    {
      const Vector3d outward = text_pt - c;
      Vector3d up = outward;
      up.Unitize();
      ON_Plane sym_pl(text_pt + up * (1.5 * h), pl.xaxis, pl.yaxis);
      ON_Arc sym(ON_Circle(sym_pl, 0.5 * h), ON_Interval(ON_PI / 6.0, 5.0 * ON_PI / 6.0));
      kernel::NurbsCurve nc;
      if (CurveFromON(ON_ArcCurve(sym), nc)) curves.push_back(nc);
    }
    const AnnotationStyle& st = ctx.Doc().CurrentAnnotationStyle();
    const DimStyleParams sp = StyleParamsOf(st);
    GlyphSpec g;
    g.text = FormatMeasurement(len, sp.precision, sp.suffix);
    g.height = h;
    g.plane = pl;
    g.plane.SetOrigin(text_pt);
    g.center = true;
    std::map<std::string, std::string> tags;
    tags["DimArcLength"] = FormatNumber(len);
    tags["DimArcRadius"] = FormatNumber(r);
    tags["DimArcCenter"] = PointTag(c);
    ctx.Doc().BeginChange("DimArc");
    const int grp = AddAnnotationGroup(ctx, "DimArc", curves, g, -1, tags);
    if (grp < 0) { Finish(); return; }
    ctx.Print("DimArc " + g.text + " (radius " + FormatNumber(r) + ", sweep " + FormatNumber(sweep * 180.0 / ON_PI) + " deg)");
    Finish();
  }
  bool partial_ = false, have_ = false;
  ON_Arc arc_;
  std::vector<Point3d> pts_;
};

// ---------------------------------------------------------------------------
// DimDisassociate
// ---------------------------------------------------------------------------

void DoDimDisassociate(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  int converted = 0, already = 0, locked = 0, other = 0;
  ctx.Doc().BeginChange("DimDisassociate");
  for (int g : AnnotationGroupsOf(ctx, ids)) {
    const std::string kind = GroupAnnotationKind(ctx.Doc(), g);
    if (kind.compare(0, 3, "Dim") != 0) { ++other; continue; }
    bool any_locked = false;
    for (const SceneObject& o : ctx.Doc().Objects()) if (o.group_id == g && ctx.Doc().IsObjectLocked(o)) any_locked = true;
    if (any_locked) { ++locked; continue; }  // only dimensions on unlocked layers (and objects) are processed
    bool had = false;
    for (SceneObject& o : ctx.Doc().Objects()) {
      if (o.group_id != g) continue;
      for (const char* k : {"DimRefObj1", "DimRefEnd1", "DimRefObj2", "DimRefEnd2"}) {
        if (o.user_text.erase(k)) had = true;
      }
    }
    if (had) ++converted; else ++already;
  }
  ctx.Doc().Touch();
  ctx.Print("DimDisassociate: " + std::to_string(converted) + " dimension(s) disassociated, " + std::to_string(already + locked + other) + " skipped (" +
            std::to_string(already) + " not associative, " + std::to_string(locked) + " locked, " + std::to_string(other) + " not dimensions)");
}

}  // namespace

void RegisterDraftDocsCommands(CommandEngine& e) {
  Reg(e, "TCount", Make<TCountCommand>(), CommandStatus::Implemented,
      "AutoCAD's TCOUNT (Express Tools): numbers the selected single-line texts. Start=<n> Increment=<n> Order=Selection|X|Y (X and Y ascending; ties keep the selection order) Mode=Overwrite|Prefix|Suffix|Replace Prefix=<text> Suffix=<text> Find=<text>. The label is Prefix + number + Suffix; Overwrite replaces the text, Prefix / Suffix put the label before / after the existing text, Replace swaps every occurrence of Find for the label (texts without Find are left alone and use no number). Multi-line (MText) objects and dimension text are not numbered.");
  Reg(e, "TextAlign", Make<TextAlignCommand>(), CommandStatus::Implemented,
      "AutoCAD's TEXTALIGN: moves the selected single-line texts so one edge of their layout box (advance width x text height, centred texts included) lines up with the same edge of the base text. Mode=Left|Center|Right|Top|Middle|Bottom; only the aligned axis moves, so the other placement is kept. The base is picked at the second prompt, or is the first selected text with Base=First. Distribute and spacing options are not implemented.");
  Reg(e, "Txt2Mtxt", Make<Txt2MtxtCommand>(), CommandStatus::Implemented,
      "AutoCAD's TXT2MTXT: Combine=Yes (default) merges the selected single-line texts into ONE multi-line text object, Order=TopDown (default, larger Y first, then X) or Selection; Combine=No converts each text on its own. The result keeps the first line's text height and baseline position, lines are 1.5 text heights apart (the layout the DXF MTEXT reader uses), and ExportDxf writes it as one MTEXT. Word wrap, mixed sizes and fonts inside one object are not implemented.");
  Reg(e, "HatchToBack", Immediate([](CommandContext& ctx) { DoHatchToBack(ctx); }), CommandStatus::Implemented,
      "AutoCAD's HATCHTOBACK: gives every hatch (solid, pattern, bitmap, section-view hatching) a draw order behind every other object, locked layers included. Uses the same per-object DrawOrder as BringToFront / SendToBack.");
  Reg(e, "SpaceTrans", Make<SpaceTransCommand>(), CommandStatus::Implemented,
      "AutoCAD's SPACETRANS: converts a length with a layout detail's scale (page millimetres per model unit): To=Model (default) turns a paper length into model units, To=Paper the reverse. Detail=<name>, or the selected / only detail of the active layout. A free-zoom detail (Scale 0) is refused.");
  Reg(e, "DimContinue", Make<DimChainCommand>(true), CommandStatus::Implemented,
      "AutoCAD's DIMCONTINUE: select a linear or aligned dimension, then pick second extension line origins; each new dimension starts at the second extension line of the previous one and shares its dimension line, so the chain is collinear. Enter finishes.");
  Reg(e, "DimBaseline", Make<DimChainCommand>(false), CommandStatus::Implemented,
      "AutoCAD's DIMBASELINE: select a linear or aligned dimension, then pick second extension line origins; each new dimension shares the first extension line of the base dimension and its dimension line is Spacing (default 3.75, AutoCAD's DIMDLI) beyond the previous one, away from the measured points. Enter finishes.");
  Reg(e, "DimSpace", Make<DimSpaceCommand>(), CommandStatus::Implemented,
      "AutoCAD's DIMSPACE: select the base dimension, then the parallel linear or aligned dimensions to space. Spacing=<n> puts their dimension lines n apart, in their current order on each side of the base; Spacing=0 aligns them with the base; Spacing=Auto (default) uses twice the base dimension's text height. Dimensions not parallel to the base are skipped.");
  Reg(e, "DimArc", Make<DimArcCommand>(), CommandStatus::Implemented,
      "AutoCAD's DIMARC: arc-length dimension of a selected arc, with a dimension arc through the picked location, radial extension lines, arrowheads and an arc symbol above the text. Partial=Yes measures only between two picked points on the arc. Polyline arc segments are not selectable here (only arc curves).");
  Reg(e, "DimDisassociate", OnSelection("Select dimensions to disassociate", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { DoDimDisassociate(ctx, ids); }), CommandStatus::Implemented,
      "AutoCAD's DIMDISASSOCIATE: removes the object anchors from the selected dimensions; they keep their current geometry and no longer follow the objects they measured (UpdateDimensions then rebuilds them from their own recorded points). Dimensions that are not associative, are locked, or are not dimensions are skipped and counted in the report.");
}

}  // namespace dino8::app
