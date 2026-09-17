// Second wave of 2D drafting tools (see AUDIT.md #10/#21/#25): the full
// hatch pattern library (with a Hatch panel), tables (Table, RevisionTable,
// TitleBlock, BillOfMaterials), GD&T (FeatureControlFrame, DatumFeature,
// SurfaceFinish, WeldSymbol), MultiLeader, DimTolerance, and live hatched
// section views (SectionView / UpdateSectionViews).
//
// Registered after RegisterAnnotate2Commands (see Application.cpp): the
// Hatch entry here replaces cmd_drafting.cpp's, everything else is new.
// Geometry helpers live in src/drafting/*.{h,cpp} so this file stays a
// thin layer of Command subclasses over them.
#include "commands/annotate_common.h"
#include "commands/cmd_common.h"
#include "commands/hatch_common.h"
#include "drafting/Gdt.h"
#include "drafting/HatchBuild.h"
#include "drafting/HatchLibrary.h"
#include "drafting/SectionView.h"
#include "drafting/Table.h"
#include "ui/Panels.h"
#include "util/json_mini.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "imgui.h"

namespace dino8::app {

using drafting::GdtSymbol;
using drafting::HatchLibrary;
using drafting::HatchPattern;
using drafting::TableSpec;

namespace {

// ---------------------------------------------------------------------------
// Small local helpers (deliberately not shared with cmd_annotate*.cpp: this
// file only reads document state through the public Document/SceneObject
// API so it can sit alongside those files without touching them).
// ---------------------------------------------------------------------------

std::string TrimWs(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::vector<std::string> SplitChar(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
  out.push_back(cur);
  return out;
}

bool IsYesLocal(const std::string& v) {
  const std::string l = ToLower(v);
  return l == "yes" || l == "on" || l == "true" || l == "1";
}

bool ParseColorOpt(const std::string& s, Color& out) {
  std::vector<std::string> f = SplitChar(s, ',');
  if (f.size() < 3) return false;
  out = Color::FromBytes(std::atoi(f[0].c_str()), std::atoi(f[1].c_str()), std::atoi(f[2].c_str()));
  return true;
}

void AddArrowLocal(std::vector<kernel::NurbsCurve>& out, Point3d tip, Vector3d dir, double size, const ON_Plane& pl) {
  dir.Unitize();
  Vector3d side = ON_CrossProduct(pl.zaxis, dir);
  side.Unitize();
  const Point3d a = tip - dir * size + side * (size * 0.3), b = tip - dir * size - side * (size * 0.3);
  out.push_back(PolylineCurve({a, tip, b, a}));
}

// Encodes/decodes a list of object ids as "id,id,id" for a user_text tag
// (BomRefIds - the associative link a table keeps to its source objects).
std::string IdsTag(const std::vector<ObjectId>& ids) {
  std::string s;
  for (ObjectId id : ids) { if (!s.empty()) s += ","; s += std::to_string(id); }
  return s;
}
std::vector<ObjectId> ParseIdsTag(const std::string& s) {
  std::vector<ObjectId> out;
  std::string cur;
  auto flush = [&]() { if (!cur.empty()) { out.push_back(static_cast<ObjectId>(std::strtoull(cur.c_str(), nullptr, 10))); cur.clear(); } };
  for (char c : s) { if (c == ',') flush(); else cur += c; }
  flush();
  return out;
}

std::string PointsTag(const std::vector<Point3d>& pts) {
  std::string s;
  for (const Point3d& p : pts) { if (!s.empty()) s += ";"; s += PointTag(p); }
  return s;
}

std::string UniqueHatchMaterialName(const Document& doc, const std::string& base) {
  if (!doc.FindMaterial(base)) return base;
  for (int i = 2; i < 10000; ++i) {
    const std::string n = base + " " + std::to_string(i);
    if (!doc.FindMaterial(n)) return n;
  }
  return base;
}

// ---------------------------------------------------------------------------
// DataLink: CSV two-way sync for a table's cell data (see RegisterDrafting2Commands
// for the DataLink/DataLinkUpdate command doc comments - the design rationale,
// including why this is CSV and not native .xlsx, lives there).
// ---------------------------------------------------------------------------

// Milliseconds since the Unix epoch - not just whole seconds - because a
// table-side change (BuildTableGroup's TableModifiedAt) and a sync
// (last_sync_utc) can genuinely happen within the same wall-clock second in
// normal use (e.g. DataLinkUpdate pulling a file and then a script/panel
// immediately editing the table again): at one-second resolution those two
// events could tie, and a "> last_sync_utc" comparison would then wrongly
// read as "nothing changed".
//
// Millisecond resolution alone only makes that collision rare, not
// impossible: a DataLinkUpdate pull (which sets last_sync_utc) immediately
// followed by a TableEdit (which sets the table's own modified-at stamp) in
// the SAME script/process can still land in the same millisecond on a fast
// machine - confirmed as a real, reproducible failure (not a load artifact)
// via the datalink_script2.txt/smoke.sh sequence, where both writes happen
// within one process invocation, only microseconds apart. A monotonic
// ratchet (never return a value <= the last one returned) makes every call
// to this function strictly increasing regardless of how close together
// they're made, which is what a "modified at" timestamp used purely for
// ordering comparisons actually needs - it does not need to reflect the
// real wall clock down to the millisecond, only to order correctly.
long long NowMillis() {
  static long long last = 0;
  long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  if (now <= last) now = last + 1;
  last = now;
  return now;
}

// std::filesystem::file_time_type isn't convertible to time_t/system_clock
// via any portable API until C++20's clock_cast. The accepted C++17
// workaround (used widely, e.g. libstdc++'s own docs) is to convert via the
// two clocks' "now" offset: it is exact to within the (negligible,
// sub-microsecond) time between the two now() calls.
bool FileMtimeMillis(const std::string& path, long long& out) {
  std::error_code ec;
  const auto ftime = std::filesystem::last_write_time(path, ec);
  if (ec) return false;
  const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
  out = std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
  return true;
}

// Looks up a user-text tag on any member of a group (every table command tags
// every grid-line object identically, so any one of them is enough - same
// approach as LoadTableSpec/TableKindOf above).
bool GroupTag(Document& doc, int group_id, const char* key, std::string& out) {
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    auto it = o.user_text.find(key);
    if (it != o.user_text.end()) { out = it->second; return true; }
  }
  return false;
}

std::string JsonEscapeLocal(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      default: out += c;
    }
  }
  return out;
}

// DataLink metadata: which external CSV file a table is linked to, the
// direction mode the caller pinned it to (or "Ask" to keep letting
// DataLinkUpdate decide from mtimes each time), and the wall-clock time of
// the last successful sync in either direction, so a later DataLinkUpdate has
// an honest "has either side changed since then" baseline.
struct DataLinkInfo { std::string path, mode = "Ask"; long long last_sync_utc = 0; };

std::string DataLinkJson(const DataLinkInfo& l) {
  std::ostringstream ss;
  ss << "{\"path\":\"" << JsonEscapeLocal(l.path) << "\",\"mode\":\"" << JsonEscapeLocal(l.mode) << "\",\"last_sync_utc\":" << l.last_sync_utc << "}";
  return ss.str();
}

bool ParseDataLinkJson(const std::string& json, DataLinkInfo& l) {
  dino8::json::Value v;
  std::string err;
  if (!dino8::json::Parse(json, v, err) || !v.IsObject()) return false;
  l.path = v["path"].AsString();
  l.mode = v["mode"].AsString("Ask");
  l.last_sync_utc = static_cast<long long>(v["last_sync_utc"].number);
  return !l.path.empty();
}

// Minimal RFC-4180 CSV writer: quotes a field only when it needs it (contains
// a comma, quote or newline), doubling embedded quotes. Cell text in a
// TableSpec is a bare string (no formulas, no styling - see Table.h), so this
// is a lossless round trip of exactly what a table can represent; it is not,
// and cannot be, a round trip of a *spreadsheet* formula, since Dino8 has
// nowhere to store one. What a pull reads back from a formula cell is
// whatever last computed text the spreadsheet program wrote to the CSV on its
// last save - a frozen value, not a live computation - and that is an
// inherent limitation of a plain-text-grid format, not a bug here.
std::string CsvField(const std::string& s) {
  const bool needs_quotes = s.find_first_of(",\"\n\r") != std::string::npos;
  if (!needs_quotes) return s;
  std::string out = "\"";
  for (char c : s) { if (c == '"') out += "\"\""; else out += c; }
  out += "\"";
  return out;
}

bool WriteCsvFile(const std::string& path, const TableSpec& spec) {
  std::ofstream f(path, std::ios::trunc);
  if (!f) return false;
  for (int r = 0; r < spec.rows; ++r) {
    for (int c = 0; c < spec.cols; ++c) f << (c ? "," : "") << CsvField(spec.Cell(r, c));
    f << "\n";
  }
  return true;
}

// Parses one RFC-4180-minimal CSV file into a row-major grid of fields.
// Returns false (with `error` set) on a malformed file - the only failure
// mode this minimal subset can hit is an unterminated quoted field - so a
// truncated/corrupted linked file is a clear, reported error rather than
// producing a garbled table or crashing.
bool ReadCsvFile(const std::string& path, std::vector<std::vector<std::string>>& rows, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { error = "could not open '" + path + "'"; return false; }
  std::ostringstream buf;
  buf << f.rdbuf();
  const std::string text = buf.str();
  rows.clear();
  std::vector<std::string> row;
  std::string field;
  bool in_quotes = false;
  size_t i = 0;
  auto end_field = [&]() { row.push_back(field); field.clear(); };
  auto end_row = [&]() { end_field(); rows.push_back(row); row.clear(); };
  while (i < text.size()) {
    const char c = text[i];
    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < text.size() && text[i + 1] == '"') { field += '"'; i += 2; continue; }
        in_quotes = false; ++i; continue;
      }
      field += c; ++i; continue;
    }
    if (c == '"' && field.empty()) { in_quotes = true; ++i; continue; }
    if (c == ',') { end_field(); ++i; continue; }
    if (c == '\r') { ++i; continue; }  // CRLF or lone CR - drop, LF (below) ends the row
    if (c == '\n') { end_row(); ++i; continue; }
    field += c; ++i;
  }
  if (in_quotes) { error = "'" + path + "' has an unterminated quoted field"; return false; }
  if (!field.empty() || !row.empty()) end_row();
  // Drop a single trailing blank line (the writer above always ends the file
  // with a final "\n", which otherwise parses as one bogus empty row).
  if (!rows.empty() && rows.back().size() == 1 && rows.back()[0].empty()) rows.pop_back();
  if (rows.empty()) { error = "'" + path + "' has no data rows"; return false; }
  return true;
}

// Resizes `spec` to the CSV's shape (a linked spreadsheet, unlike TableEdit's
// Data=, has no obligation to match the table's current row/column count -
// same "the pull is the new truth" behavior Excel itself shows when you
// re-open a data range whose source changed shape) and fills its cells from
// it, clamped to the same sane 1..10000 bound ParseTableDataJson enforces
// against a hostile/corrupt file. Presentation fields (title, col_widths,
// row_height, text_height, origin, plane) are untouched - a CSV has no
// equivalent for any of them (see the DataLink command doc comment).
void ApplyCsvToSpec(const std::vector<std::vector<std::string>>& rows, TableSpec& spec) {
  size_t max_cols = 1;
  for (const auto& r : rows) max_cols = std::max(max_cols, r.size());
  spec.rows = std::clamp(static_cast<int>(rows.size()), 1, 10000);
  spec.cols = std::clamp(static_cast<int>(max_cols), 1, 10000);
  spec.cells.assign(static_cast<size_t>(spec.rows) * static_cast<size_t>(spec.cols), "");
  for (int r = 0; r < spec.rows && static_cast<size_t>(r) < rows.size(); ++r)
    for (int c = 0; c < spec.cols && static_cast<size_t>(c) < rows[static_cast<size_t>(r)].size(); ++c)
      spec.cells[static_cast<size_t>(r) * static_cast<size_t>(spec.cols) + static_cast<size_t>(c)] = rows[static_cast<size_t>(r)][static_cast<size_t>(c)];
}

// ---------------------------------------------------------------------------
// Tables: shared build / rebuild.
// ---------------------------------------------------------------------------

void ParseTableData(const std::string& data, TableSpec& spec) {
  spec.cells.assign(static_cast<size_t>(std::max(0, spec.rows)) * static_cast<size_t>(std::max(0, spec.cols)), "");
  if (data.empty()) return;
  std::vector<std::string> rows = SplitChar(data, ';');
  for (size_t r = 0; r < rows.size() && static_cast<int>(r) < spec.rows; ++r) {
    std::vector<std::string> cols = SplitChar(rows[r], ',');
    for (size_t c = 0; c < cols.size() && static_cast<int>(c) < spec.cols; ++c) spec.cells[r * static_cast<size_t>(spec.cols) + c] = TrimWs(cols[c]);
  }
}

void ParseColWidths(const std::string& s, TableSpec& spec) {
  spec.col_widths.clear();
  if (s.empty()) return;
  for (const std::string& t : SplitChar(s, ',')) spec.col_widths.push_back(std::atof(t.c_str()));
}

// Builds a table's grid lines + cell-text glyphs as one annotation group,
// tagging every grid line with the JSON cell data and the plane the table
// sits on, so TableEdit / the Table Editor panel can rebuild it. Fills in
// spec's height defaults in place before storing them.
int BuildTableGroup(CommandContext& ctx, TableSpec spec, const std::string& kind, int layer = -1,
                    const std::map<std::string, std::string>& extra_tags = {}) {
  if (layer < 0) layer = DimensionLayer(ctx);
  const std::string style = ctx.Settings().annotation_style;
  if (spec.text_height <= 0) spec.text_height = AnnotationTextHeight(ctx);
  if (spec.row_height <= 0) spec.row_height = spec.text_height * 2.2;
  if (static_cast<int>(spec.col_widths.size()) < spec.cols) spec.col_widths.resize(static_cast<size_t>(spec.cols), 0);
  if (static_cast<int>(spec.cells.size()) < spec.rows * spec.cols) spec.cells.resize(static_cast<size_t>(spec.rows) * spec.cols, "");

  std::vector<double> xs(static_cast<size_t>(spec.cols) + 1, 0.0);
  for (int c = 0; c < spec.cols; ++c) xs[static_cast<size_t>(c) + 1] = xs[static_cast<size_t>(c)] + spec.ColWidth(c);
  const double total_w = xs.back();
  const double title_h = spec.title.empty() ? 0.0 : spec.row_height;
  const double total_h = title_h + spec.row_height * spec.rows;
  auto PT = [&](double x, double down) { return spec.origin + spec.plane.xaxis * x - spec.plane.yaxis * down; };

  std::vector<kernel::NurbsCurve> lines;
  if (title_h > 0) lines.push_back(PolylineCurve({PT(0, 0), PT(total_w, 0)}));
  for (int r = 0; r <= spec.rows; ++r) { const double y = title_h + r * spec.row_height; lines.push_back(PolylineCurve({PT(0, y), PT(total_w, y)})); }
  for (int c = 0; c <= spec.cols; ++c) lines.push_back(PolylineCurve({PT(xs[static_cast<size_t>(c)], 0), PT(xs[static_cast<size_t>(c)], total_h)}));

  std::vector<ObjectId> ids;
  const std::string json = drafting::TableDataJson(spec);
  const std::string ox = PointTag(spec.origin), tx = PointTag(Point3d(spec.plane.xaxis)), ty = PointTag(Point3d(spec.plane.yaxis));
  // TableModifiedAt is the "this group's cell data was last (re)built" wall-clock
  // timestamp - not shown to the user, just read back by DataLinkUpdate to tell
  // whether the *table* side has changed since a link's last_sync_utc, the other
  // half of the push/pull direction heuristic (see DataLinkUpdateCommand below).
  const std::string modified_at = std::to_string(NowMillis());
  for (const kernel::NurbsCurve& c : lines) {
    SceneObject s = SceneObject::MakeCurve(c);
    s.layer_index = layer;
    TagAnnotation(s, kind, style);
    s.user_text["TableData"] = json;
    s.user_text["TableOrigin"] = ox;
    s.user_text["TableX"] = tx;
    s.user_text["TableY"] = ty;
    s.user_text["TableModifiedAt"] = modified_at;
    for (const auto& [k, v] : extra_tags) s.user_text[k] = v;
    ids.push_back(ctx.Doc().Add(std::move(s)));
  }

  const double pad = spec.text_height * 0.35;
  auto add_text = [&](const std::string& text, double x, double y_top) {
    if (text.empty()) return;
    GlyphSpec g;
    g.text = text;
    g.height = std::max(1e-6, spec.text_height * 0.62);
    g.plane = spec.plane;
    g.plane.SetOrigin(PT(x, y_top + spec.row_height * 0.5 + g.height * 0.35));
    for (ObjectId id : AddGlyphCurves(ctx, g, layer, -1, {{"Annotation", kind}, {"Style", style}})) ids.push_back(id);
  };
  if (title_h > 0) add_text(spec.title, pad, 0);
  for (int r = 0; r < spec.rows; ++r)
    for (int c = 0; c < spec.cols; ++c) add_text(spec.Cell(r, c), xs[static_cast<size_t>(c)] + pad, title_h + r * spec.row_height);

  if (ids.empty()) return -1;
  return ctx.Doc().CreateGroup(ids, kind);
}

// Reads a table group's cell data and plane back from its tags (any one
// member carrying "TableData" is enough).
bool LoadTableSpec(Document& doc, int group_id, TableSpec& spec) {
  for (const SceneObject& o : doc.Objects()) {
    if (o.group_id != group_id) continue;
    auto it = o.user_text.find("TableData");
    if (it == o.user_text.end() || !drafting::ParseTableDataJson(it->second, spec)) continue;
    Point3d org, ax, ay;
    auto oit = o.user_text.find("TableOrigin"), xit = o.user_text.find("TableX"), yit = o.user_text.find("TableY");
    if (oit != o.user_text.end() && ParsePointTag(oit->second, org)) spec.origin = org;
    if (xit != o.user_text.end() && yit != o.user_text.end() && ParsePointTag(xit->second, ax) && ParsePointTag(yit->second, ay))
      spec.plane = ON_Plane(spec.origin, Vector3d(ax.x, ax.y, ax.z), Vector3d(ay.x, ay.y, ay.z));
    return true;
  }
  return false;
}

std::string TableKindOf(Document& doc, int group_id) {
  for (const SceneObject& o : doc.Objects())
    if (o.group_id == group_id) { auto it = o.user_text.find("Annotation"); if (it != o.user_text.end()) return it->second; }
  return "Table";
}

class TableCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    spec_.rows = std::max(1, std::atoi(OptionOr(opts, "rows", "2").c_str()));
    spec_.cols = std::max(1, std::atoi(OptionOr(opts, "cols", "2").c_str()));
    ParseTableData(OptionOr(opts, "data", ""), spec_);
    ParseColWidths(OptionOr(opts, "colwidths", ""), spec_);
    spec_.text_height = std::atof(OptionOr(opts, "textheight", "0").c_str());
    spec_.title = OptionOr(opts, "title", "");
    options = {{"Rows", std::to_string(spec_.rows), {}, true, false}, {"Cols", std::to_string(spec_.cols), {}, true, false}};
    WantPoint("Top-left corner of the table");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Rows") { spec_.rows = std::max(1, std::atoi(v.c_str())); options[0].value = std::to_string(spec_.rows); }
    else if (n == "Cols") { spec_.cols = std::max(1, std::atoi(v.c_str())); options[1].value = std::to_string(spec_.cols); }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    spec_.origin = p;
    spec_.plane = ActivePlane(ctx);
    ctx.Doc().BeginChange("Table");
    const int g = BuildTableGroup(ctx, spec_, "Table");
    ctx.Print("Table: " + std::to_string(spec_.rows) + "x" + std::to_string(spec_.cols) + " table" + (g < 0 ? " (failed)" : " created"));
    Finish();
  }
  TableSpec spec_;
};

class TableEditCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    group_id_ = std::atoi(OptionOr(opts, "groupid", "-1").c_str());
    data_ = OptionOr(opts, "data", "");
    rows_ = OptionOr(opts, "rows", "");
    cols_ = OptionOr(opts, "cols", "");
    has_title_ = opts.count("title") > 0;
    title_ = OptionOr(opts, "title", "");
    if (group_id_ >= 0) { Run(ctx); Finish(); return; }
    WantObjects("Select a table (Table / RevisionTable / TitleBlock / BillOfMaterials) to edit");
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id); o && o->user_text.count("TableData")) { group_id_ = o->group_id; break; }
    if (group_id_ < 0) { ctx.Warn("TableEdit: selection has no table"); Finish(); return; }
    Run(ctx);
    Finish();
  }
  void Run(CommandContext& ctx) {
    TableSpec spec;
    const std::string kind = TableKindOf(ctx.Doc(), group_id_);
    if (!LoadTableSpec(ctx.Doc(), group_id_, spec)) { ctx.Warn("TableEdit: table not found"); return; }
    if (!rows_.empty()) spec.rows = std::max(1, std::atoi(rows_.c_str()));
    if (!cols_.empty()) spec.cols = std::max(1, std::atoi(cols_.c_str()));
    if (!data_.empty()) ParseTableData(data_, spec);
    else spec.cells.resize(static_cast<size_t>(spec.rows) * spec.cols, "");
    if (has_title_) spec.title = title_;
    // A DataLink survives a plain TableEdit (the group is fully torn down and
    // rebuilt below, same as every other table rebuild) - otherwise editing a
    // linked table by hand through the Table Editor panel would silently and
    // irreversibly drop its link. See DataLinkCommand's doc comment.
    std::map<std::string, std::string> pass_through;
    std::string link_tag;
    if (GroupTag(ctx.Doc(), group_id_, "DataLink", link_tag)) pass_through["DataLink"] = link_tag;
    ctx.Doc().BeginChange("TableEdit");
    for (ObjectId id : ctx.Doc().GroupMembers(group_id_)) ctx.Doc().Remove(id);
    BuildTableGroup(ctx, spec, kind, -1, pass_through);
    ctx.Print("TableEdit: table rebuilt (" + std::to_string(spec.rows) + "x" + std::to_string(spec.cols) + ")");
  }
  int group_id_ = -1;
  std::string data_, rows_, cols_, title_;
  bool has_title_ = false;
};

// DataLink / DataLinkUpdate: AutoCAD-style two-way sync between a table's
// cells and an external CSV file. First increment deliberately targets CSV,
// not native .xlsx: a TableSpec cell is a bare string (Table.h - no formulas,
// no styles, no merges), so nothing in the current table model can be lost by
// not using a heavier spreadsheet-container format, and CSV read/write needs
// no new dependency (BillOfMaterialsCommand above already writes one by hand
// with std::ofstream). A later increment could add real .xlsx via a vendored
// MIT library the way LibreDWG was vendored for real .dwg, if a need for
// preserving a workbook's *other* sheets/styles ever comes up - CSV cannot do
// that, since writing a CSV always replaces the whole file.
//
// Direction is never guessed silently once a table has synced before: if
// only the file changed since last_sync_utc, DataLinkUpdate pulls; if only
// the table changed, it pushes; if both changed, it refuses and asks for an
// explicit Direction=Push|Pull, mirroring AutoCAD's own DATALINKUPDATE, which
// is itself a manual, on-demand, non-silent operation - not a background
// live sync (out of scope here; a poll-on-idle "file changed, run
// DataLinkUpdate" status hint would be a reasonable later increment).
//
// Formula caveat: pulling from a real spreadsheet only ever sees the last
// value that program itself wrote to the CSV on save. A cell holding
// "=SUM(A1:A2)" arrives here as whatever number Excel/etc last computed for
// it, frozen - never the live formula. This is an inherent limit of a
// plain-text-grid format, disclosed rather than silently papered over.
bool FindTableGroup(CommandContext& ctx, const std::vector<ObjectId>& ids, int& group_id) {
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id); o && o->user_text.count("TableData")) { group_id = o->group_id; return true; }
  return false;
}

// Rewrites a table group's DataLink tag and, when `new_spec` differs from
// what's on disk (a pull), its cell data too - by the same
// remove-every-member-then-BuildTableGroup path every other table rebuild in
// this file uses, so undo/redo, layer, and every other bit of group state
// stays consistent with the rest of the Table family. `link.last_sync_utc`
// must already be its final value: BuildTableGroup's remove+CreateGroup
// mints a *new* group id (same as every other table rebuild here - see
// TableEditCommand::Run), so there is no group-id-stable way to go back and
// patch the tag afterwards the way a plain in-place user_text edit could.
//
// This rebuild's own BuildTableGroup call sets a fresh TableModifiedAt on
// the group it just (re)built - that is itself a "table-side change" by
// BuildTableGroup's own definition, but it is NOT an independent edit that
// happened after this sync; it *is* this sync. If it were left to pick up
// its own NowMillis() call (later than `link.last_sync_utc`'s, thanks to
// NowMillis()'s monotonic ratchet - see that function's own doc comment),
// DataLinkUpdate's very next run would see table_mtime > last_sync_utc and
// wrongly think the table changed again immediately after syncing it. Pin
// both timestamps to the exact same value here so they compare equal (not
// ">"), which DataLinkUpdate correctly reads as "not changed since this
// sync" - explicitly overriding BuildTableGroup's own TableModifiedAt via
// extra_tags (applied after its default in that function, so this wins).
void RebuildWithLink(CommandContext& ctx, int group_id, TableSpec spec, const std::string& kind, DataLinkInfo link, const std::string& change_name) {
  link.last_sync_utc = NowMillis();
  ctx.Doc().BeginChange(change_name);
  for (ObjectId id : ctx.Doc().GroupMembers(group_id)) ctx.Doc().Remove(id);
  BuildTableGroup(ctx, spec, kind, -1, {{"DataLink", DataLinkJson(link)}, {"TableModifiedAt", std::to_string(link.last_sync_utc)}});
}

class DataLinkCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    group_id_ = std::atoi(OptionOr(opts, "groupid", "-1").c_str());
    file_ = OptionOr(opts, "file", "");
    mode_ = OptionOr(opts, "mode", "Ask");
    if (group_id_ >= 0) { Run(ctx); Finish(); return; }
    WantObjects("Select a table to link to a CSV file");
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!FindTableGroup(ctx, ids, group_id_)) { ctx.Warn("DataLink: selection has no table"); Finish(); return; }
    Run(ctx);
    Finish();
  }
  void Run(CommandContext& ctx) {
    if (file_.empty()) { ctx.Warn("DataLink: File=<path.csv> is required"); return; }
    TableSpec spec;
    const std::string kind = TableKindOf(ctx.Doc(), group_id_);
    if (!LoadTableSpec(ctx.Doc(), group_id_, spec)) { ctx.Warn("DataLink: table not found"); return; }

    std::error_code ec;
    const bool exists = std::filesystem::exists(file_, ec) && !ec;
    bool push;
    if (!exists) {
      // Nothing to pull from yet - always an initial push, same as AutoCAD
      // DATALINK writing out the table's current data the first time it
      // targets a source that doesn't exist on disk.
      push = true;
    } else if (ToLower(mode_) == "push") {
      push = true;
    } else if (ToLower(mode_) == "pull") {
      push = false;
    } else {
      // Mode=Ask (default) and the file already exists: there is no prior
      // last_sync_utc yet to compare against (this is the *first* sync), so
      // fall back to comparing the file's mtime against this table's own
      // last-(re)build time - whichever side has the more recently produced
      // content wins the direction, same spirit as DataLinkUpdate's
      // steady-state heuristic below but with "table creation" standing in
      // for "last sync".
      long long csv_mtime = 0, table_mtime = 0;
      std::string mod_tag;
      FileMtimeMillis(file_, csv_mtime);
      if (GroupTag(ctx.Doc(), group_id_, "TableModifiedAt", mod_tag)) table_mtime = std::atoll(mod_tag.c_str());
      push = table_mtime > csv_mtime;  // ties (or an unreadable mtime) favor pulling the existing file's content in
    }

    if (push) {
      if (!WriteCsvFile(file_, spec)) { ctx.Warn("DataLink: could not write '" + file_ + "'"); return; }
    } else {
      std::vector<std::vector<std::string>> rows;
      std::string err;
      if (!ReadCsvFile(file_, rows, err)) { ctx.Warn("DataLink: " + err); return; }
      ApplyCsvToSpec(rows, spec);
    }
    DataLinkInfo link{file_, mode_, NowMillis()};
    RebuildWithLink(ctx, group_id_, spec, kind, link, "DataLink");
    ctx.Print("DataLink: " + std::string(push ? "pushed " : "pulled ") + std::to_string(spec.rows) + "x" + std::to_string(spec.cols) +
               " table " + (push ? "to " : "from ") + file_);
  }
  int group_id_ = -1;
  std::string file_, mode_;
};

class DataLinkUpdateCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    group_id_ = std::atoi(OptionOr(opts, "groupid", "-1").c_str());
    direction_ = OptionOr(opts, "direction", "");
    if (group_id_ >= 0) { Run(ctx); Finish(); return; }
    WantObjects("Select a linked table to re-sync");
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!FindTableGroup(ctx, ids, group_id_)) { ctx.Warn("DataLinkUpdate: selection has no table"); Finish(); return; }
    Run(ctx);
    Finish();
  }
  void Run(CommandContext& ctx) {
    std::string link_json;
    DataLinkInfo link;
    if (!GroupTag(ctx.Doc(), group_id_, "DataLink", link_json) || !ParseDataLinkJson(link_json, link)) {
      ctx.Warn("DataLinkUpdate: table is not linked to a file (use DataLink first)");
      return;
    }
    TableSpec spec;
    const std::string kind = TableKindOf(ctx.Doc(), group_id_);
    if (!LoadTableSpec(ctx.Doc(), group_id_, spec)) { ctx.Warn("DataLinkUpdate: table not found"); return; }

    std::error_code ec;
    if (!std::filesystem::exists(link.path, ec) || ec) {
      // A deleted/moved linked file is a clear, reported error - not a crash
      // and not silently falling back to a push that would recreate it
      // without the user asking for that.
      ctx.Warn("DataLinkUpdate: linked file '" + link.path + "' no longer exists");
      return;
    }

    bool push;
    if (!direction_.empty()) {
      if (ToLower(direction_) == "push") push = true;
      else if (ToLower(direction_) == "pull") push = false;
      else { ctx.Warn("DataLinkUpdate: Direction must be Push or Pull"); return; }
    } else {
      long long csv_mtime = 0, table_mtime = 0;
      std::string mod_tag;
      const bool have_mtime = FileMtimeMillis(link.path, csv_mtime);
      if (GroupTag(ctx.Doc(), group_id_, "TableModifiedAt", mod_tag)) table_mtime = std::atoll(mod_tag.c_str());
      const bool csv_changed = have_mtime && csv_mtime > link.last_sync_utc;
      const bool table_changed = table_mtime > link.last_sync_utc;
      if (!csv_changed && !table_changed) { ctx.Print("DataLinkUpdate: already up to date (" + link.path + ")"); return; }
      if (csv_changed && table_changed) {
        // Both sides moved since the last sync - never guess which one wins;
        // AutoCAD's own DATALINKUPDATE never silently overwrites unsaved
        // changes on either side either.
        ctx.Warn("DataLinkUpdate: both the table and '" + link.path + "' changed since the last sync (file mtime " +
                  std::to_string(csv_mtime) + ", table edited " + std::to_string(table_mtime) + ", last sync " +
                  std::to_string(link.last_sync_utc) + ") - re-run with Direction=Push or Direction=Pull to pick one");
        return;
      }
      push = table_changed;  // exactly one side changed
    }

    if (push) {
      if (!WriteCsvFile(link.path, spec)) { ctx.Warn("DataLinkUpdate: could not write '" + link.path + "'"); return; }
    } else {
      std::vector<std::vector<std::string>> rows;
      std::string err;
      if (!ReadCsvFile(link.path, rows, err)) { ctx.Warn("DataLinkUpdate: " + err); return; }
      ApplyCsvToSpec(rows, spec);
    }
    link.last_sync_utc = NowMillis();
    RebuildWithLink(ctx, group_id_, spec, kind, link, "DataLinkUpdate");
    ctx.Print("DataLinkUpdate: " + std::string(push ? "pushed " : "pulled ") + std::to_string(spec.rows) + "x" + std::to_string(spec.cols) +
               " table " + (push ? "to " : "from ") + link.path);
  }
  int group_id_ = -1;
  std::string direction_;
};

class RevisionTableCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    add_ = OptionOr(opts, "add", "");
    WantPoint("Revision table location (Enter to reuse or just append a row)");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override { origin_ = p; has_point_ = true; Run(ctx); Finish(); }
  void OnEnter(CommandContext& ctx) override { Run(ctx); Finish(); }
  void Run(CommandContext& ctx) {
    ctx.Doc().BeginChange("RevisionTable");
    int group = -1;
    for (const SceneObject& o : ctx.Doc().Objects()) if (auto it = o.user_text.find("Annotation"); it != o.user_text.end() && it->second == "RevisionTable") { group = o.group_id; break; }
    TableSpec spec;
    std::map<std::string, std::string> pass_through;
    if (group >= 0 && LoadTableSpec(ctx.Doc(), group, spec)) {
      std::string link_tag;
      if (GroupTag(ctx.Doc(), group, "DataLink", link_tag)) pass_through["DataLink"] = link_tag;
      for (ObjectId id : ctx.Doc().GroupMembers(group)) ctx.Doc().Remove(id);
    } else {
      spec.cols = 3;
      spec.rows = 1;
      spec.cells = {"Rev", "Date", "Description"};
      spec.col_widths = {12, 24, 60};
      spec.title = "Revisions";
      spec.plane = ActivePlane(ctx);
      spec.origin = has_point_ ? origin_ : Point3d(0, 0, 0);
    }
    if (!add_.empty()) {
      std::vector<std::string> f = SplitChar(add_, ',');
      while (f.size() < 3) f.push_back("");
      spec.rows++;
      for (int i = 0; i < 3; ++i) spec.cells.push_back(TrimWs(f[static_cast<size_t>(i)]));
    }
    BuildTableGroup(ctx, spec, "RevisionTable", -1, pass_through);
    ctx.Print("RevisionTable: " + std::to_string(spec.rows - 1) + " revision(s)");
  }
  std::string add_;
  bool has_point_ = false;
  Point3d origin_{0, 0, 0};
};

// PanelSchedule (Electrical vertical-market toolset, elec/ElecComponents.h -
// see that header's own scope comment): a real data table of electrical
// panel circuit rows {circuit #, description, load VA}, built through the
// exact same TableSpec/BuildTableGroup mechanism as RevisionTable/
// BillOfMaterials above rather than a new table format. DATA TABLE ONLY -
// no breaker-sizing, phase load-balancing, or other panel-schedule
// engineering calculation, and no NEC/IEC code-compliance check (same
// explicit-scope discipline as MepDiameterFromFlow's own doc comment in
// ArchComponents.h).
class PanelScheduleCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    circuits_ = OptionOr(opts, "circuits", "");
    name_ = OptionOr(opts, "name", "Panel A");
    WantPoint("Panel schedule location (top-left corner)");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    TableSpec spec;
    spec.cols = 3;
    spec.cells = {"Circuit #", "Description", "Load (VA)"};
    spec.col_widths = {20, 64, 24};
    spec.title = name_ + " - Panel Schedule";
    int rows = 1;
    for (const std::string& row : SplitChar(circuits_, ';')) {
      std::vector<std::string> f = SplitChar(row, ',');
      while (f.size() < 3) f.push_back("");
      for (int c = 0; c < 3; ++c) spec.cells.push_back(TrimWs(f[static_cast<size_t>(c)]));
      ++rows;
    }
    spec.rows = rows;
    spec.origin = p;
    spec.plane = ActivePlane(ctx);
    ctx.Doc().BeginChange("PanelSchedule");
    const int g = BuildTableGroup(ctx, spec, "PanelSchedule");
    ctx.Print("PanelSchedule: " + std::to_string(spec.rows - 1) + " circuit row(s)" + (g < 0 ? " (failed)" : " built"));
    Finish();
  }

 private:
  std::string circuits_, name_;
};

class TitleBlockCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    name_ = OptionOr(opts, "name", ctx.Doc().Settings().title.empty() ? "Untitled" : ctx.Doc().Settings().title);
    date_ = OptionOr(opts, "date", "");
    scale_ = OptionOr(opts, "scale", "1:1");
    const Layout* L = ctx.App().ActiveLayout();
    sheet_ = OptionOr(opts, "sheet", L ? L->name : std::string("Model"));
    WantPoint("Title block location (top-left corner)");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    TableSpec spec;
    spec.cols = 2;
    spec.rows = 4;
    spec.col_widths = {24, 56};
    spec.cells = {"Name", name_, "Date", date_, "Scale", scale_, "Sheet", sheet_};
    spec.origin = p;
    spec.plane = ActivePlane(ctx);
    ctx.Doc().BeginChange("TitleBlock");
    BuildTableGroup(ctx, spec, "TitleBlock");
    ctx.Print("TitleBlock: " + name_ + " (Sheet " + sheet_ + ", Scale " + scale_ + ")");
    Finish();
  }
  std::string name_, date_, scale_, sheet_;
};

// Aggregates `ids` into a BillOfMaterials TableSpec (rows/cols/cells only -
// caller fills in origin/plane). Shared by BillOfMaterialsCommand and
// UpdateBillOfMaterials so a re-derive from the same (or a re-scanned) id
// list produces byte-identical logic to the original bake. `csv_rows`, when
// given, is filled with the same rows for the optional CSV report.
struct BomRow { std::string key, layer, material; int qty = 0; double length = 0, area = 0, volume = 0; };
TableSpec BuildBomSpec(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::string& by, std::vector<BomRow>* csv_rows = nullptr) {
  std::map<std::string, BomRow> rows;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->user_text.count("Annotation") || o->user_text.count("Hatch")) continue;  // skip drafting output itself
    std::string block;
    if (auto it = o->user_text.find("Block"); it != o->user_text.end()) block = it->second;
    const std::string layer = (o->layer_index >= 0 && o->layer_index < static_cast<int>(ctx.Doc().Layers().size())) ? ctx.Doc().Layers()[static_cast<size_t>(o->layer_index)].name : "";
    const std::string mat = o->material_name;
    const std::string name = !block.empty() ? block : (!o->name.empty() ? o->name : ObjectKindName(o->kind));
    const std::string key = by == "layer" ? (layer.empty() ? "(none)" : layer) : by == "material" ? (mat.empty() ? "(none)" : mat) : name;
    BomRow& r = rows[key];
    r.key = key;
    r.layer = layer;
    r.material = mat;
    ++r.qty;
    if (o->kind == ObjectKind::Curve && o->curve) r.length += o->curve->Length();
    else if (auto m = MeshOf(*o)) { r.area += m->Area(); if (m->IsClosedManifold()) r.volume += std::fabs(m->Volume()); }
  }
  TableSpec spec;
  spec.cols = 6;
  spec.col_widths = {40, 14, 26, 26, 24, 20};
  spec.cells = {"Item", "Qty", "Layer", "Material", "Length/Area", "Volume"};
  spec.rows = 1;
  for (const auto& [k, r] : rows) {
    (void)k;
    ++spec.rows;
    spec.cells.push_back(r.key);
    spec.cells.push_back(std::to_string(r.qty));
    spec.cells.push_back(r.layer.empty() ? "-" : r.layer);
    spec.cells.push_back(r.material.empty() ? "-" : r.material);
    spec.cells.push_back(r.length > 0 ? FormatNumber(r.length) + " L" : r.area > 0 ? FormatNumber(r.area) + " A" : "-");
    spec.cells.push_back(r.volume > 0 ? FormatNumber(r.volume) : "-");
    if (csv_rows) csv_rows->push_back(r);
  }
  spec.title = "Bill of Materials";
  return spec;
}

// A BillOfMaterials table is associative: the group carries
//   BomRefIds = the exact object ids it was built from (explicit selection), or
//   BomAll    = "1" (built from "every visible object" - Enter), no BomRefIds
//   BomBy     = "name"/"layer"/"material" grouping key
// so UpdateBillOfMaterials (below) can re-derive its rows from the *current*
// state of those objects (or of the document, for BomAll) - count, name,
// material, length/area/volume - instead of the numbers staying frozen at
// creation time. A row's numbers track a live edit to its source objects;
// what the table cannot know is which *new* objects should count under an
// explicit id list (only Enter's "every visible object" mode re-scans for
// newcomers) - documented in the command's registration note below.
class BillOfMaterialsCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    by_ = ToLower(OptionOr(opts, "by", "name"));
    csv_ = OptionOr(opts, "csv", "");
    WantObjects("Select objects for the bill of materials (Enter for every visible object)");
  }
  void OnEnter(CommandContext& ctx) override {
    std::vector<ObjectId> ids;
    for (const SceneObject& o : ctx.Doc().Objects()) if (ctx.Doc().IsObjectVisible(o)) ids.push_back(o.id);
    Run(ctx, ids, /*all=*/true);
    Finish();
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override { Run(ctx, ids, /*all=*/false); Finish(); }
  void Run(CommandContext& ctx, const std::vector<ObjectId>& ids, bool all) {
    std::vector<BomRow> csv_rows;
    TableSpec spec = BuildBomSpec(ctx, ids, by_, csv_.empty() ? nullptr : &csv_rows);
    spec.origin = ctx.HoverPoint().value_or(Point3d(0, 0, 0));
    spec.plane = ActivePlane(ctx);
    ctx.Doc().BeginChange("BillOfMaterials");
    std::map<std::string, std::string> tags = {{"BomBy", by_}};
    if (all) tags["BomAll"] = "1"; else tags["BomRefIds"] = IdsTag(ids);
    BuildTableGroup(ctx, spec, "BillOfMaterials", -1, tags);
    if (!csv_.empty()) {
      std::ofstream f(csv_);
      f << "Item,Qty,Layer,Material,Length,Area,Volume\n";
      for (const BomRow& r : csv_rows) f << r.key << "," << r.qty << "," << r.layer << "," << r.material << "," << r.length << "," << r.area << "," << r.volume << "\n";
    }
    ctx.Print("BillOfMaterials: " + std::to_string(spec.rows - 1) + " row(s)" + (csv_.empty() ? "" : ", CSV written to " + csv_));
  }
  std::string by_, csv_;
};

// ---------------------------------------------------------------------------
// GD&T
// ---------------------------------------------------------------------------

std::string Utf8Circled(char letter) {
  const unsigned cp = 0x24B6u + static_cast<unsigned>(std::toupper(static_cast<unsigned char>(letter)) - 'A');
  std::string s(3, '\0');
  s[0] = static_cast<char>(0xE0 | (cp >> 12));
  s[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  s[2] = static_cast<char>(0x80 | (cp & 0x3F));
  return s;
}
const char* const kDiameterSign = "\xE2\x8C\x80";  // U+2300
const char* const kPlusMinus = "\xC2\xB1";          // U+00B1

class FeatureControlFrameCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    if (!ParseGdtSymbol(OptionOr(opts, "symbol", "Flatness"), symbol_)) symbol_ = GdtSymbol::Flatness;
    tolerance_ = OptionOr(opts, "tolerance", "0.05");
    datums_ = OptionOr(opts, "datums", "");
    modifier_ = OptionOr(opts, "modifier", "");
    diameter_ = IsYesLocal(OptionOr(opts, "diameter", "No"));
    std::vector<std::string> names = drafting::GdtSymbolNames();
    options = {{"Symbol", drafting::GdtSymbolName(symbol_), names, false, false}, {"Tolerance", tolerance_, {}, true, false}, {"Datums", datums_, {}, false, false}};
    WantPoint("Point on the feature (leader start)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Symbol") { GdtSymbol s; if (ParseGdtSymbol(v, s)) { symbol_ = s; options[0].value = drafting::GdtSymbolName(symbol_); } }
    else if (n == "Tolerance") { tolerance_ = v; options[1].value = v; }
    else if (n == "Datums") { datums_ = v; options[2].value = v; }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) WantPoint("Frame location");
    else Build(ctx);
  }
  void Build(CommandContext& ctx) {
    ctx.ClearPreview();
    const ON_Plane pl = ActivePlane(ctx);
    const double h = AnnotationTextHeight(ctx);
    const std::string style = ctx.Settings().annotation_style;
    const int layer = DimensionLayer(ctx);
    std::vector<ObjectId> ids;
    auto add_curves = [&](const std::vector<kernel::NurbsCurve>& cs) {
      for (const kernel::NurbsCurve& c : cs) { SceneObject s = SceneObject::MakeCurve(c); s.layer_index = layer; TagAnnotation(s, "FeatureControlFrame", style); ids.push_back(ctx.Doc().Add(std::move(s))); }
    };
    // Leader from the feature to the frame.
    const Point3d land = pts_[1];
    std::vector<kernel::NurbsCurve> leader = {PolylineCurve({pts_[0], land})};
    AddArrowLocal(leader, pts_[0], pts_[0] - land, h * 0.6, pl);
    add_curves(leader);

    // Frame: symbol cell, tolerance cell, one cell per datum letter.
    std::vector<std::string> datum_list;
    for (const std::string& d : SplitChar(datums_, ',')) if (!TrimWs(d).empty()) datum_list.push_back(TrimWs(d));
    std::string tol_text = (diameter_ ? std::string(kDiameterSign) : std::string()) + tolerance_;
    if (!modifier_.empty()) tol_text += " " + Utf8Circled(modifier_[0]);
    const double cell_h = h * 1.8;
    std::vector<double> w = {cell_h, std::max(cell_h * 1.5, h * 0.75 * static_cast<double>(tol_text.size()) * 0.55 + cell_h * 0.4)};
    for (size_t i = 0; i < datum_list.size(); ++i) w.push_back(cell_h);
    std::vector<double> xs = {0};
    for (double v : w) xs.push_back(xs.back() + v);
    const double total_w = xs.back();
    auto PT = [&](double x, double y) { return land + pl.xaxis * x - pl.yaxis * y; };
    std::vector<kernel::NurbsCurve> box;
    box.push_back(PolylineCurve({PT(0, 0), PT(total_w, 0)}));
    box.push_back(PolylineCurve({PT(0, cell_h), PT(total_w, cell_h)}));
    for (double x : xs) box.push_back(PolylineCurve({PT(x, 0), PT(x, cell_h)}));
    add_curves(box);
    std::vector<kernel::NurbsCurve> glyph;
    const double gsz = cell_h * 0.62;
    drafting::AppendGdtGlyph(symbol_, PT(xs[0] + (xs[1] - xs[0] - gsz) * 0.5, cell_h * 0.19 + gsz), pl, gsz, glyph);
    add_curves(glyph);
    GlyphSpec tol_g;
    tol_g.text = tol_text;
    tol_g.height = cell_h * 0.5;
    tol_g.plane = pl;
    tol_g.plane.SetOrigin(PT(xs[1] + cell_h * 0.18, cell_h * 0.68));
    for (ObjectId id : AddGlyphCurves(ctx, tol_g, layer, -1, {{"Annotation", "FeatureControlFrame"}, {"Style", style}})) ids.push_back(id);
    for (size_t i = 0; i < datum_list.size(); ++i) {
      GlyphSpec dg;
      dg.text = datum_list[i];
      dg.height = cell_h * 0.55;
      dg.center = true;
      dg.plane = pl;
      dg.plane.SetOrigin(PT((xs[2 + i] + xs[3 + i]) * 0.5, cell_h * 0.68));
      for (ObjectId id : AddGlyphCurves(ctx, dg, layer, -1, {{"Annotation", "FeatureControlFrame"}, {"Style", style}})) ids.push_back(id);
    }
    ctx.Doc().BeginChange("FeatureControlFrame");
    ctx.Doc().CreateGroup(ids, "FeatureControlFrame");
    ctx.Print("FeatureControlFrame: " + std::string(drafting::GdtSymbolName(symbol_)) + " " + tol_text + (datum_list.empty() ? "" : " | " + datums_));
    Finish();
  }
  GdtSymbol symbol_ = GdtSymbol::Flatness;
  std::string tolerance_, datums_, modifier_;
  bool diameter_ = false;
  std::vector<Point3d> pts_;
};

class DatumFeatureCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    letter_ = OptionOr(opts, "letter", "");
    WantPoint("Point on the feature");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    origin_ = p;
    if (letter_.empty()) { WantText("Datum letter", "A"); return; }
    Build(ctx);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { letter_ = t.empty() ? "A" : t.substr(0, 1); Build(ctx); }
  void Build(CommandContext& ctx) {
    const ON_Plane pl = ActivePlane(ctx);
    const double h = AnnotationTextHeight(ctx);
    const std::string style = ctx.Settings().annotation_style;
    const int layer = DimensionLayer(ctx);
    std::vector<kernel::NurbsCurve> curves;
    drafting::AppendDatumTriangle(origin_, pl, h, curves);
    ctx.Doc().BeginChange("DatumFeature");
    std::vector<ObjectId> ids;
    for (const kernel::NurbsCurve& c : curves) { SceneObject s = SceneObject::MakeCurve(c); s.layer_index = layer; TagAnnotation(s, "DatumFeature", style); ids.push_back(ctx.Doc().Add(std::move(s))); }
    GlyphSpec g;
    g.text = letter_;
    g.height = h * 0.7;
    g.center = true;
    g.plane = pl;
    g.plane.SetOrigin(origin_ + pl.yaxis * (h * 2.5));
    for (ObjectId id : AddGlyphCurves(ctx, g, layer, -1, {{"Annotation", "DatumFeature"}, {"Style", style}})) ids.push_back(id);
    ctx.Doc().CreateGroup(ids, "DatumFeature");
    ctx.Print("DatumFeature: '" + letter_ + "'");
    Finish();
  }
  std::string letter_;
  Point3d origin_{0, 0, 0};
};

class SurfaceFinishCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    value_ = OptionOr(opts, "value", "");
    WantPoint("Point on the surface");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    const ON_Plane pl = ActivePlane(ctx);
    const double h = AnnotationTextHeight(ctx);
    const std::string style = ctx.Settings().annotation_style;
    const int layer = DimensionLayer(ctx);
    std::vector<kernel::NurbsCurve> curves;
    drafting::AppendSurfaceFinishGlyph(p, pl, h, curves);
    ctx.Doc().BeginChange("SurfaceFinish");
    std::vector<ObjectId> ids;
    for (const kernel::NurbsCurve& c : curves) { SceneObject s = SceneObject::MakeCurve(c); s.layer_index = layer; TagAnnotation(s, "SurfaceFinish", style); ids.push_back(ctx.Doc().Add(std::move(s))); }
    if (!value_.empty()) {
      GlyphSpec g;
      g.text = "Ra " + value_;
      g.height = h * 0.5;
      g.plane = pl;
      g.plane.SetOrigin(p + pl.yaxis * (h * 0.7) + pl.xaxis * (h * 1.1));
      for (ObjectId id : AddGlyphCurves(ctx, g, layer, -1, {{"Annotation", "SurfaceFinish"}, {"Style", style}})) ids.push_back(id);
    }
    ctx.Doc().CreateGroup(ids, "SurfaceFinish");
    ctx.Print("SurfaceFinish" + (value_.empty() ? std::string() : ": Ra " + value_));
    Finish();
  }
  std::string value_;
};

class WeldSymbolCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    type_ = OptionOr(opts, "type", "Fillet");
    side_ = OptionOr(opts, "side", "Above");
    options = {{"Type", type_, {"Fillet", "Groove", "Spot"}, false, false}, {"Side", side_, {"Above", "Below"}, false, false}};
    WantPoint("Arrow point (on the joint)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Type") { type_ = v; options[0].value = v; }
    else if (n == "Side") { side_ = v; options[1].value = v; }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) WantPoint("Reference line end");
    else Build(ctx);
  }
  void Build(CommandContext& ctx) {
    const ON_Plane pl = ActivePlane(ctx);
    const double h = AnnotationTextHeight(ctx);
    const std::string style = ctx.Settings().annotation_style;
    const int layer = DimensionLayer(ctx);
    std::vector<kernel::NurbsCurve> curves = {PolylineCurve({pts_[0], pts_[1]})};
    AddArrowLocal(curves, pts_[0], pts_[0] - pts_[1], h * 0.6, pl);
    drafting::WeldSymbolType wt = drafting::WeldSymbolType::Fillet;
    drafting::ParseWeldSymbolType(type_, wt);
    drafting::AppendWeldGlyph(pts_[1], pl, h, ToLower(side_) != "below", wt, curves);
    ctx.Doc().BeginChange("WeldSymbol");
    std::vector<ObjectId> ids;
    for (const kernel::NurbsCurve& c : curves) { SceneObject s = SceneObject::MakeCurve(c); s.layer_index = layer; TagAnnotation(s, "WeldSymbol", style); ids.push_back(ctx.Doc().Add(std::move(s))); }
    ctx.Doc().CreateGroup(ids, "WeldSymbol");
    ctx.Print("WeldSymbol: " + type_ + " (" + side_ + ")");
    Finish();
  }
  std::string type_, side_;
  std::vector<Point3d> pts_;
};

class MultiLeaderCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Arrow point 1"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (stage_ == 0) { pts_.push_back(p); ctx.SetLastPoint(p); WantPoint("Next arrow point (Enter when done)"); return; }
    if (stage_ == 1) { landing_ = p; ctx.SetLastPoint(p); stage_ = 2; WantText("Leader text"); return; }
  }
  void OnEnter(CommandContext&) override {
    if (stage_ == 0 && !pts_.empty()) { stage_ = 1; WantPoint("Landing point"); return; }
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (stage_ != 2) return;
    const ON_Plane pl = ActivePlane(ctx);
    const double h = AnnotationTextHeight(ctx);
    const std::string style = ctx.Settings().annotation_style;
    const int layer = DimensionLayer(ctx);
    std::vector<kernel::NurbsCurve> curves;
    for (const Point3d& pt : pts_) curves.push_back(PolylineCurve({pt, landing_}));
    for (const Point3d& pt : pts_) AddArrowLocal(curves, pt, pt - landing_, h * 0.6, pl);
    ctx.Doc().BeginChange("MultiLeader");
    std::vector<ObjectId> ids;
    for (const kernel::NurbsCurve& c : curves) {
      SceneObject s = SceneObject::MakeCurve(c);
      s.layer_index = layer;
      TagAnnotation(s, "MultiLeader", style);
      s.user_text["MLeaderPoints"] = PointsTag(pts_);
      ids.push_back(ctx.Doc().Add(std::move(s)));
    }
    GlyphSpec g;
    g.text = t;
    g.height = h;
    g.plane = pl;
    g.plane.SetOrigin(landing_ + pl.xaxis * (h * 0.3));
    for (ObjectId id : AddGlyphCurves(ctx, g, layer, -1, {{"Annotation", "MultiLeader"}, {"Style", style}})) ids.push_back(id);
    ctx.Doc().CreateGroup(ids, "MultiLeader");
    ctx.Print("MultiLeader: " + std::to_string(pts_.size()) + " arrow(s), \"" + t + "\"");
    Finish();
  }
  int stage_ = 0;
  std::vector<Point3d> pts_;
  Point3d landing_{0, 0, 0};
};

class DimToleranceCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    mode_ = ToLower(OptionOr(opts, "type", "symmetric"));
    value_ = OptionOr(opts, "value", "0.05");
    upper_ = OptionOr(opts, "upper", "");
    lower_ = OptionOr(opts, "lower", "");
    WantObjects("Select dimensions to add a tolerance to");
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    std::vector<int> groups = AnnotationGroupsOf(ctx, ids);
    ctx.Doc().BeginChange("DimTolerance");
    int done = 0;
    for (int g : groups) {
      GlyphSpec spec;
      if (!GroupGlyphSpec(ctx, g, spec)) continue;
      // The pre-tolerance text, stamped on the glyph the first time this
      // runs and carried forward by RebuildGroupText after that - so a
      // second DimTolerance call replaces the suffix instead of appending
      // another one onto whatever is currently displayed.
      std::string base = spec.text;
      for (const SceneObject& o : ctx.Doc().Objects()) {
        if (o.group_id != g || !o.user_text.count("Glyph")) continue;
        auto it = o.user_text.find("DimTolerance.Base");
        if (it != o.user_text.end()) { base = it->second; break; }
      }
      std::string suffix;
      if (mode_ == "limits") suffix = " " + (upper_.empty() ? value_ : upper_) + "/-" + (lower_.empty() ? value_ : lower_);
      else if (mode_ == "deviation") suffix = " +" + (upper_.empty() ? value_ : upper_) + "/-" + (lower_.empty() ? value_ : lower_);
      else suffix = std::string(" ") + kPlusMinus + value_;
      spec.text = base + suffix;
      if (RebuildGroupText(ctx, g, spec, {"DimTolerance.Base"}) > 0) {
        for (SceneObject& o : ctx.Doc().Objects()) if (o.group_id == g && o.user_text.count("Glyph")) o.user_text["DimTolerance.Base"] = base;
        ++done;
      }
    }
    ctx.Print("DimTolerance: " + std::to_string(done) + " dimension(s) updated");
    Finish();
  }
  std::string mode_, value_, upper_, lower_;
};

// ---------------------------------------------------------------------------
// Hatch: re-registers the Hatch command against the pattern library.
// ---------------------------------------------------------------------------

bool AddSolidHatch(CommandContext& ctx, const kernel::NurbsCurve& boundary, ObjectId boundary_id, int layer, const Color* color) {
  return drafting::BuildSolidHatch(ctx.Doc(), boundary, boundary_id, layer, ctx.Settings().absolute_tolerance, color);
}

bool AddBitmapHatch(CommandContext& ctx, const kernel::NurbsCurve& boundary, ObjectId boundary_id, int layer, const std::string& image) {
  ON_Plane pl;
  if (!boundary.raw().IsPlanar(&pl, ctx.Settings().absolute_tolerance)) return false;
  const std::vector<Point3d> poly = BoundaryPolygon(boundary);
  if (poly.empty()) return false;
  double umin = 1e300, umax = -1e300, vmin = 1e300, vmax = -1e300;
  for (const Point3d& p : poly) { double u, v; pl.ClosestPointTo(p, &u, &v); umin = std::min(umin, u); umax = std::max(umax, u); vmin = std::min(vmin, v); vmax = std::max(vmax, v); }
  const std::vector<Point3d> grid = {pl.PointAt(umin, vmin), pl.PointAt(umax, vmin), pl.PointAt(umin, vmax), pl.PointAt(umax, vmax)};
  Document& doc = ctx.Doc();
  Material m;
  m.name = UniqueHatchMaterialName(doc, std::filesystem::path(image).stem().string());
  m.diffuse = Color::FromBytes(255, 255, 255);
  m.gloss = 0.05f;
  m.texture_path = image;
  m.mapping = TextureMapping::Surface;
  const std::string mname = doc.AddMaterial(m);
  SceneObject s = SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1));
  s.name = "Hatch Bitmap";
  s.layer_index = layer;
  s.material_name = mname;
  s.user_text["Hatch"] = "Bitmap";
  s.user_text["HatchImage"] = image;
  s.user_text["HatchBoundary"] = std::to_string(boundary_id);
  doc.CreateGroup({doc.Add(std::move(s))}, "Hatch");
  return true;
}

class LibraryHatchCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    pattern_name_ = OptionOr(opts, "pattern", "ANSI31");
    scale_ = std::atof(OptionOr(opts, "scale", "1").c_str());
    if (scale_ <= 0) scale_ = 1;
    rotation_ = std::atof(OptionOr(opts, "rotation", "45").c_str());
    image_ = OptionOr(opts, "image", "");
    Color c;
    has_color_ = ParseColorOpt(OptionOr(opts, "color", ""), c);
    if (has_color_) color_ = c;
    std::vector<std::string> names = HatchLibrary::Instance().Names();
    if (std::find(names.begin(), names.end(), "Bitmap") == names.end()) names.push_back("Bitmap");
    options = {{"Pattern", pattern_name_, names, false, false}, {"Scale", FormatNumber(scale_), {}, true, false}, {"Rotation", FormatNumber(rotation_), {}, true, false}};
    WantObjects("Select closed planar curves for hatch boundaries");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Pattern") { pattern_name_ = v; options[0].value = v; }
    else if (n == "Scale") { const double s = std::atof(v.c_str()); if (s > 0) { scale_ = s; options[1].value = FormatNumber(scale_); } }
    else if (n == "Rotation") { rotation_ = std::atof(v.c_str()); options[2].value = FormatNumber(rotation_); }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    struct Boundary { kernel::NurbsCurve curve; ObjectId id; int layer; };
    std::vector<Boundary> curves;
    for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id); o && o->kind == ObjectKind::Curve && o->curve->IsClosed()) curves.push_back({*o->curve, o->id, o->layer_index});
    if (curves.empty()) { ctx.Warn("Select closed planar curves"); Finish(); return; }
    ctx.Doc().BeginChange("Hatch");
    int made = 0;
    const bool bitmap = ToLower(pattern_name_) == "bitmap" && !image_.empty();
    const bool solid = ToLower(pattern_name_) == "solid";
    const HatchPattern* pat = (bitmap || solid) ? nullptr : HatchLibrary::Instance().Find(pattern_name_);
    for (const Boundary& b : curves) {
      if (solid) { if (AddSolidHatch(ctx, b.curve, b.id, b.layer, has_color_ ? &color_ : nullptr)) ++made; continue; }
      if (bitmap) { if (AddBitmapHatch(ctx, b.curve, b.id, b.layer, image_)) ++made; continue; }
      if (!pat) continue;
      bool truncated = false;
      if (!drafting::BuildPatternHatch(ctx.Doc(), *pat, b.curve, b.id, b.layer, ctx.Settings().absolute_tolerance, scale_,
                                        rotation_, ctx.Settings().hatch_base, has_color_ ? &color_ : nullptr, &truncated))
        continue;
      ++made;
      if (truncated) ctx.Warn("Hatch: pattern density was truncated for one boundary");
    }
    ctx.Print("Hatch: " + std::to_string(made) + " boundary(ies) hatched (" + pattern_name_ + ")");
    Finish();
  }
  std::string pattern_name_ = "ANSI31", image_;
  double scale_ = 1, rotation_ = 45;
  Color color_;
  bool has_color_ = false;
};

// ---------------------------------------------------------------------------
// Live hatched section views.
// ---------------------------------------------------------------------------

// Slices every visible, non-annotation object with `pl`, chains the pieces,
// hatches the closed loops with `pattern_name` (the library default when
// empty or unknown), tags the group with the plane and pattern so
// UpdateSectionViews can regenerate it, and returns the object count.
int BuildSectionGroup(CommandContext& ctx, const ON_Plane& pl, const std::string& pattern_name, double scale, const std::string& plane_name) {
  std::vector<ObjectId> ids;
  const HatchPattern* pat = HatchLibrary::Instance().Find(pattern_name.empty() ? "ANSI31" : pattern_name);
  const double tol = ctx.Settings().absolute_tolerance * 10;
  int layer = ctx.Doc().FindLayer("Sections");
  if (layer < 0) layer = ctx.Doc().AddLayer("Sections", Color::FromBytes(200, 60, 60));
  const std::string style = ctx.Settings().annotation_style;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(o) || o.user_text.count("Annotation") || o.user_text.count("Hatch")) continue;
    std::optional<kernel::Mesh> m = MeshOf(o, 0.01);
    if (!m) continue;
    for (std::vector<Point3d> chain : drafting::SliceMeshToChains(m->raw(), pl, tol)) {
      if (chain.size() < 2) continue;
      const bool closed = chain.size() > 2 && chain.front().DistanceTo(chain.back()) <= tol * 10;
      if (closed) chain.back() = chain.front();
      SceneObject s = SceneObject::MakeCurve(PolylineCurve(chain));
      s.layer_index = layer;
      TagAnnotation(s, "SectionView", style);
      s.color = Color::FromBytes(200, 30, 30);
      s.color_by_layer = false;
      ids.push_back(ctx.Doc().Add(std::move(s)));
      if (closed && pat) {
        const std::vector<drafting::Loop> loops = {chain};
        for (const kernel::NurbsCurve& c : drafting::HatchPatternCurves(*pat, loops, pl, scale, 0, ctx.Settings().hatch_base)) {
          SceneObject hs = SceneObject::MakeCurve(c);
          hs.layer_index = layer;
          TagAnnotation(hs, "SectionView", style);
          hs.user_text["Hatch"] = pat->name;
          ids.push_back(ctx.Doc().Add(std::move(hs)));
        }
      }
    }
  }
  if (ids.empty()) return 0;
  const std::string ox = PointTag(pl.origin), tx = PointTag(Point3d(pl.xaxis)), ty = PointTag(Point3d(pl.yaxis));
  for (ObjectId id : ids) {
    if (SceneObject* o = ctx.Doc().Find(id)) {
      o->user_text["SectionOrigin"] = ox;
      o->user_text["SectionX"] = tx;
      o->user_text["SectionY"] = ty;
      o->user_text["SectionHatch"] = pat ? pat->name : "ANSI31";
      o->user_text["SectionScale"] = FormatNumber(scale);
      if (!plane_name.empty()) o->user_text["SectionPlaneName"] = plane_name;
    }
  }
  ctx.Doc().CreateGroup(ids, "SectionView");
  return static_cast<int>(ids.size());
}

class SectionViewCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    auto opts = TakeOptionTokens(ctx);
    plane_name_ = OptionOr(opts, "plane", "");
    pattern_ = OptionOr(opts, "hatch", "ANSI31");
    scale_ = std::atof(OptionOr(opts, "scale", "1").c_str());
    if (scale_ <= 0) scale_ = 1;
    if (!plane_name_.empty()) { FromNamedPlane(ctx); return; }
    WantPoint("First point of the section line");
  }
  void FromNamedPlane(CommandContext& ctx) {
    ClippingPlane* cp = nullptr;
    for (ClippingPlane& c : ctx.Doc().ClippingPlanes()) if (ToLower(c.name) == ToLower(plane_name_)) cp = &c;
    if (!cp) { ctx.Warn("SectionView: no clipping plane named '" + plane_name_ + "'"); Finish(); return; }
    ctx.Doc().BeginChange("SectionView");
    const int made = BuildSectionGroup(ctx, ON_Plane(cp->origin, cp->x_axis, cp->y_axis), pattern_, scale_, cp->name);
    ctx.Print("SectionView: " + std::to_string(made) + " curve(s) from plane '" + cp->name + "'");
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() < 2) { WantPoint("Second point of the section line"); return; }
    const ON_Plane base = ActivePlane(ctx);
    Vector3d dir = pts_[1] - pts_[0];
    if (dir.Length() < 1e-9) dir = base.xaxis;
    dir.Unitize();
    const ON_Plane cut(pts_[0], dir, base.zaxis);
    ctx.Doc().BeginChange("SectionView");
    const int made = BuildSectionGroup(ctx, cut, pattern_, scale_, "");
    ctx.Print("SectionView: " + std::to_string(made) + " curve(s)");
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { ctx.ClearPreview(); if (!pts_.empty()) ctx.AddPreviewLine(pts_[0], h); }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<Point3d> pts_;
  std::string plane_name_, pattern_;
  double scale_ = 1;
};

}  // namespace

void RegisterDrafting2Commands(CommandEngine& e) {
  // Hatch itself draws from the real data/hatchpatterns.pat library (AutoCAD
  // .pat syntax) plus solid and bitmap fills - fully implemented. HatchScale
  // (cmd_annotate2.cpp) still rebuilds boundaries against an older, separate
  // 4-pattern set rather than this library; that is HatchScale's gap, not
  // this command's, and out of scope here (a different file).
  Reg(e, "Hatch", Make<LibraryHatchCommand>());

  Reg(e, "Table", Make<TableCommand>());
  Reg(e, "TableEdit", Make<TableEditCommand>());
  Reg(e, "DataLink", Make<DataLinkCommand>(), CommandStatus::Implemented,
      "Links a table to an external CSV file and does the initial sync (push if the file doesn't exist yet, otherwise Push/Pull/whichever side's content looks newer for Mode=Ask) - CSV, not native .xlsx, since a TableSpec cell is a bare string with no formulas/styles/merges to lose either way; pulling from a real spreadsheet only ever sees the last value it wrote to the CSV on save, never a live formula.");
  Reg(e, "DataLinkUpdate", Make<DataLinkUpdateCommand>(), CommandStatus::Implemented,
      "Re-syncs an already-linked table on demand: pulls if only the file changed since the last sync, pushes if only the table did, and refuses to guess (asking for an explicit Direction=Push|Pull) if both changed - manual and on-demand like AutoCAD's own DATALINKUPDATE, not a background file watcher.");
  Reg(e, "RevisionTable", Make<RevisionTableCommand>());
  // Baked curve/surface geometry instead of a live entity is this app's
  // established, accepted shape for every annotation (see cmd_annotate.cpp
  // and cmd_annotate2.cpp's DimArea/DimCurveLength/DimVolume/DimOrdinate/
  // DimCreaseAngle, all Implemented with the identical caveat) - not a gap
  // unique to these five, so they are marked Implemented rather than
  // singled out as Partial for sharing it.
  Reg(e, "TitleBlock", Make<TitleBlockCommand>(), CommandStatus::Implemented,
      "Builds a simple Name/Date/Scale/Sheet field table, not an instance of a linked block definition - inserting one does not track edits to a shared title-block template, the same live-instancing gap as the Block command (cmd_drafting.cpp) has no fix for.");
  Reg(e, "BillOfMaterials", Make<BillOfMaterialsCommand>(), CommandStatus::Implemented,
      "Associative: the table records which objects (or 'every visible object') it was built from and UpdateBillOfMaterials re-derives every row's count/layer/material/length-area-volume from their current state. Built from an explicit selection, it re-checks only those objects (a deleted one drops out; a new object never joins on its own) - only the Enter/'every visible object' mode picks up newcomers, since only it has a re-scan rule instead of a fixed id list.");
  Reg(e, "PanelSchedule", Make<PanelScheduleCommand>(), CommandStatus::Implemented,
      "A real data table (Circuit #/Description/Load VA rows, Circuits=1,Lighting,500;2,Receptacles,900 option syntax) via the same Table/TableSpec/BuildTableGroup mechanism as RevisionTable/BillOfMaterials - NOT a panel-schedule engineering calculation (no breaker sizing, phase load-balancing, or NEC/IEC code-compliance check) and not associative to any electrical component in the drawing.");

  Reg(e, "FeatureControlFrame", Make<FeatureControlFrameCommand>(), CommandStatus::Implemented,
      "Characteristic symbols (flatness, position, etc.) are drawn as vector curves matching the ASME Y14.5 shapes; material-condition modifiers (S)/(L)/(M) use Unicode circled letters as a stand-in, since this build has no dedicated GD&T symbol font to draw the real modifier glyphs from.");
  Reg(e, "DatumFeature", Make<DatumFeatureCommand>());
  Reg(e, "SurfaceFinish", Make<SurfaceFinishCommand>());
  Reg(e, "WeldSymbol", Make<WeldSymbolCommand>(), CommandStatus::Implemented,
      "Draws the reference line, arrow and a Type-selected glyph (Fillet triangle, square-Groove bars, or Spot circle); the rest of the AWS A2.4 symbol set (bevel/V/U-groove, plug, seam, back, surfacing, ...) is not drawn.");
  Reg(e, "MultiLeader", Make<MultiLeaderCommand>(), CommandStatus::Implemented,
      "Draws several arrows converging on one landing with the shared text, correctly - but the text is baked glyph geometry like every other annotation here, not something double-click-editable in place the way TextProperties edits a live text field.");
  Reg(e, "DimTolerance", Make<DimToleranceCommand>(), CommandStatus::Implemented,
      "Appends the tolerance to the dimension's baked text and rebuilds the glyph (idempotent - re-running replaces the suffix rather than compounding it), but like every Dim* command the dimension is baked curve geometry, not a live object, so it still doesn't update if the measured geometry moves.");

  Reg(e, "SectionView", Make<SectionViewCommand>(), CommandStatus::Implemented,
      "Slices visible objects with a picked line or a named clipping plane and hatches the closed loops it finds - the cut curves themselves are coplanar so there is nothing to hide among them, but this does not additionally draw the hidden-line-removed wireframe of what lies beyond the cut plane the way Rhino's optional 'visible edges beyond' feature does.");
  Reg(e, "UpdateSectionViews", Immediate([](CommandContext& ctx) {
        std::vector<int> groups;
        for (const SceneObject& o : ctx.Doc().Objects())
          if (o.group_id >= 0 && o.user_text.count("Annotation") && o.user_text.at("Annotation") == "SectionView" && std::find(groups.begin(), groups.end(), o.group_id) == groups.end())
            groups.push_back(o.group_id);
        if (groups.empty()) { ctx.Print("UpdateSectionViews: no section views in this document"); return; }
        ctx.Doc().BeginChange("UpdateSectionViews");
        int updated = 0;
        for (int g : groups) {
          ON_Plane pl;
          bool have_plane = false;
          std::string pat_name = "ANSI31", plane_name;
          double scale = 1;
          for (const SceneObject& o : ctx.Doc().Objects()) {
            if (o.group_id != g) continue;
            Point3d org, ax, ay;
            if (o.user_text.count("SectionOrigin") && o.user_text.count("SectionX") && o.user_text.count("SectionY") &&
                ParsePointTag(o.user_text.at("SectionOrigin"), org) && ParsePointTag(o.user_text.at("SectionX"), ax) && ParsePointTag(o.user_text.at("SectionY"), ay)) {
              pl = ON_Plane(org, Vector3d(ax.x, ax.y, ax.z), Vector3d(ay.x, ay.y, ay.z));
              have_plane = true;
            }
            if (auto it = o.user_text.find("SectionHatch"); it != o.user_text.end()) pat_name = it->second;
            if (auto it = o.user_text.find("SectionScale"); it != o.user_text.end()) scale = std::atof(it->second.c_str());
            if (auto it = o.user_text.find("SectionPlaneName"); it != o.user_text.end()) plane_name = it->second;
            break;
          }
          if (!have_plane) continue;
          for (ObjectId id : ctx.Doc().GroupMembers(g)) ctx.Doc().Remove(id);
          if (BuildSectionGroup(ctx, pl, pat_name, scale, plane_name) > 0) ++updated;
        }
        ctx.Print("UpdateSectionViews: " + std::to_string(updated) + " section view(s) regenerated");
      }));

  Reg(e, "UpdateBillOfMaterials", Immediate([](CommandContext& ctx) {
        std::vector<int> groups;
        for (const SceneObject& o : ctx.Doc().Objects())
          if (o.group_id >= 0 && o.user_text.count("Annotation") && o.user_text.at("Annotation") == "BillOfMaterials" &&
              std::find(groups.begin(), groups.end(), o.group_id) == groups.end())
            groups.push_back(o.group_id);
        if (groups.empty()) { ctx.Print("UpdateBillOfMaterials: no bill-of-materials tables in this document"); return; }
        ctx.Doc().BeginChange("UpdateBillOfMaterials");
        int updated = 0;
        for (int g : groups) {
          TableSpec old;
          if (!LoadTableSpec(ctx.Doc(), g, old)) continue;  // group has no TableData - nothing to rebuild from
          std::string by = "name", ref_ids_tag;
          bool all = false;
          for (const SceneObject& o : ctx.Doc().Objects()) {
            if (o.group_id != g) continue;
            if (auto it = o.user_text.find("BomBy"); it != o.user_text.end()) by = it->second;
            if (auto it = o.user_text.find("BomAll"); it != o.user_text.end() && it->second == "1") all = true;
            if (auto it = o.user_text.find("BomRefIds"); it != o.user_text.end()) ref_ids_tag = it->second;
            break;
          }
          std::vector<ObjectId> ids;
          if (all) { for (const SceneObject& o : ctx.Doc().Objects()) if (ctx.Doc().IsObjectVisible(o)) ids.push_back(o.id); }
          else for (ObjectId id : ParseIdsTag(ref_ids_tag)) if (ctx.Doc().Find(id)) ids.push_back(id);  // drop ids of since-deleted objects
          std::vector<BomRow> rows;
          TableSpec spec = BuildBomSpec(ctx, ids, by, &rows);
          spec.origin = old.origin;
          spec.plane = old.plane;
          for (ObjectId id : ctx.Doc().GroupMembers(g)) ctx.Doc().Remove(id);
          std::map<std::string, std::string> tags = {{"BomBy", by}};
          if (all) tags["BomAll"] = "1"; else tags["BomRefIds"] = IdsTag(ids);
          if (BuildTableGroup(ctx, spec, "BillOfMaterials", -1, tags) >= 0) {
            ++updated;
            // A compact one-line-per-row summary (not the table's own glyph
            // curve objects, which can number in the dozens for a small
            // table) so a caller can confirm a row's aggregated fields - not
            // just the row count - actually reflect the current document.
            std::string summary;
            for (const BomRow& r : rows) summary += (summary.empty() ? "" : "; ") + r.key + " qty=" + std::to_string(r.qty) + " material=" + (r.material.empty() ? "(none)" : r.material);
            ctx.Print("UpdateBillOfMaterials:   " + summary);
          }
        }
        ctx.Print("UpdateBillOfMaterials: " + std::to_string(updated) + " table(s) regenerated");
      }), CommandStatus::Implemented,
      "Re-derives every BillOfMaterials table's rows (count/layer/material/length-area-volume) from the current state of the objects it was built from, replacing the old baked rows in place - the associative counterpart to BillOfMaterials's static bake, following the same explicit-recompute shape as UpdateSectionViews above rather than an automatic hook on every document edit.");
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------

void DrawHatchPatternsPanel(Application& app) {
  ImGui::SetNextWindowSize(ImVec2(420, 460), ImGuiCond_Appearing);
  if (!ImGui::Begin("Hatch Patterns", &app.Panels().hatch_patterns)) { ImGui::End(); return; }
  static double scale = 1, rotation = 45;
  ImGui::TextUnformatted("Click a pattern to hatch the current selection with it.");
  ImGui::SetNextItemWidth(90);
  ImGui::InputDouble("Scale", &scale);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  ImGui::InputDouble("Rotation", &rotation);
  if (scale <= 0) scale = 1;
  ImGui::Separator();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float thumb = 64.0f;
  int col = 0;
  for (const HatchPattern& pat : HatchLibrary::Instance().Patterns()) {
    ImGui::PushID(pat.name.c_str());
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("thumb", ImVec2(thumb, thumb));
    const bool clicked = ImGui::IsItemClicked();
    dl->AddRectFilled(p0, ImVec2(p0.x + thumb, p0.y + thumb), IM_COL32(250, 250, 250, 255));
    dl->AddRect(p0, ImVec2(p0.x + thumb, p0.y + thumb), IM_COL32(120, 120, 120, 255));
    if (pat.IsSolid()) {
      dl->AddRectFilled(ImVec2(p0.x + 3, p0.y + 3), ImVec2(p0.x + thumb - 3, p0.y + thumb - 3), IM_COL32(70, 70, 70, 255));
    } else {
      const std::vector<double> segs = drafting::HatchPatternPreview(pat, 10.0, scale, rotation, 500);
      for (size_t i = 0; i + 3 < segs.size(); i += 4) {
        const float x0 = p0.x + 3 + static_cast<float>(segs[i] / 10.0 * (thumb - 6));
        const float y0 = p0.y + thumb - 3 - static_cast<float>(segs[i + 1] / 10.0 * (thumb - 6));
        const float x1 = p0.x + 3 + static_cast<float>(segs[i + 2] / 10.0 * (thumb - 6));
        const float y1 = p0.y + thumb - 3 - static_cast<float>(segs[i + 3] / 10.0 * (thumb - 6));
        dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(20, 20, 20, 255));
      }
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + thumb));
    ImGui::TextUnformatted(pat.name.c_str());
    if (clicked) app.Engine().Execute("Hatch Pattern=" + pat.name + " Scale=" + FormatNumber(scale) + " Rotation=" + FormatNumber(rotation));
    ImGui::PopID();
    ++col;
    if (col % 4 != 0) { ImGui::SameLine(0, 24); } else { ImGui::Dummy(ImVec2(1, 4)); }
  }
  ImGui::End();
}

void DrawTableEditorPanel(Application& app) {
  ImGui::SetNextWindowSize(ImVec2(480, 420), ImGuiCond_Appearing);
  if (!ImGui::Begin("Table Editor", &app.Panels().table_editor)) { ImGui::End(); return; }
  Document& doc = app.Doc();
  static int selected_group = -1;
  std::vector<int> groups;
  for (const SceneObject& o : doc.Objects())
    if (o.group_id >= 0 && o.user_text.count("TableData") && std::find(groups.begin(), groups.end(), o.group_id) == groups.end()) groups.push_back(o.group_id);
  ImGui::TextUnformatted("Tables in this document:");
  if (ImGui::BeginChild("list", ImVec2(0, 90), true)) {
    for (int g : groups) {
      const std::string label = TableKindOf(doc, g) + " #" + std::to_string(g);
      if (ImGui::Selectable(label.c_str(), selected_group == g)) selected_group = g;
    }
  }
  ImGui::EndChild();
  ImGui::Separator();
  static int last_group = -2;
  static drafting::TableSpec editing;
  static std::vector<std::array<char, 64>> buf;
  if (selected_group != last_group) {
    last_group = selected_group;
    if (selected_group >= 0 && LoadTableSpec(doc, selected_group, editing)) {
      buf.assign(static_cast<size_t>(std::max(0, editing.rows)) * static_cast<size_t>(std::max(0, editing.cols)), {});
      for (size_t i = 0; i < editing.cells.size() && i < buf.size(); ++i) std::snprintf(buf[i].data(), buf[i].size(), "%s", editing.cells[i].c_str());
    } else {
      buf.clear();
    }
  }
  if (selected_group >= 0 && !buf.empty()) {
    if (ImGui::BeginTable("tbl", editing.cols, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY, ImVec2(0, 220))) {
      for (int r = 0; r < editing.rows; ++r) {
        ImGui::TableNextRow();
        for (int c = 0; c < editing.cols; ++c) {
          ImGui::TableSetColumnIndex(c);
          ImGui::PushID(r * editing.cols + c);
          ImGui::SetNextItemWidth(70);
          ImGui::InputText("##cell", buf[static_cast<size_t>(r * editing.cols + c)].data(), buf[static_cast<size_t>(r * editing.cols + c)].size());
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
    ImGui::TextUnformatted("Note: cell values may not contain spaces (the command line splits on whitespace).");
    if (ImGui::Button("Apply")) {
      std::string data;
      for (int r = 0; r < editing.rows; ++r) {
        if (r) data += ";";
        for (int c = 0; c < editing.cols; ++c) { if (c) data += ","; data += buf[static_cast<size_t>(r * editing.cols + c)].data(); }
      }
      app.Engine().Execute("TableEdit GroupId=" + std::to_string(selected_group) + " Data=" + data);
    }
  } else {
    ImGui::TextUnformatted("Select a table above (created by Table / RevisionTable / TitleBlock / BillOfMaterials).");
  }
  ImGui::End();
}

}  // namespace dino8::app
