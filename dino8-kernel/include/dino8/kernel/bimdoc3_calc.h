// BIM documentation logic, third batch (builds on bimdoc_calc.h): renovation status and filters, schedule schemes as XML, field catalog
// search and ordering, merged header rows, editable schedule cells with overrides, length / area formats, property sheet export and
// import, headline rows, splitting an oversized schedule, tag labels and conditional formatting.
//
// Header-only, standard library only. Behaviours the cited vendor pages do not state are marked "assumption" in the comments and in the
// notes of the Register rows.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "dino8/kernel/bimdoc_calc.h"

namespace dino8 {
namespace kernel {
namespace bimdoc {

// ------------------------------------------------------------------------------------------------------------------------------
// Renovation: status, filters (show / hide / override per status), per-view filter, pinning, opening follow, reset, XML
// ------------------------------------------------------------------------------------------------------------------------------
enum class RenoStatus { Existing = 0, Demolished = 1, New = 2 };
enum class RenoShow { Show, Hide, Override };
enum class RenoLook { Hidden, Normal, Override };

struct RenoFilter {
  std::string name;
  RenoShow show[3] = {RenoShow::Show, RenoShow::Show, RenoShow::Show};   // by RenoStatus
  std::string rule[3];                                                   // graphic override rule used when the status is Override
  bool excludeAnnotations = true;                                        // annotation elements ignore the filter and stay normal
};

struct RenoElement {
  std::string id, type;
  RenoStatus status = RenoStatus::New;
  std::string host;        // an opening's wall
  std::string pinned;      // restricted to this one filter ("" = none)
  bool annotation = false;
  double quantity = 0;     // volume or area counted in schedules
};

// How an element looks under a filter. An annotation is normal when the filter excludes annotations. An element pinned to another filter
// is hidden. Otherwise the filter's setting for the element's status decides. A pinned element whose status the filter hides stays hidden.
inline RenoLook LookUnder(const RenoElement& e, const RenoFilter& f) {
  if (e.annotation && f.excludeAnnotations) return RenoLook::Normal;
  if (!e.pinned.empty() && e.pinned != f.name) return RenoLook::Hidden;
  RenoShow s = f.show[(int)e.status];
  return s == RenoShow::Show ? RenoLook::Normal : s == RenoShow::Hide ? RenoLook::Hidden : RenoLook::Override;
}

struct RenoRemap {
  // target per source status: 0 Existing, 1 Demolished, 2 New, -1 delete the element, -2 leave the status
  int to[3] = {-2, -2, -2};
};
struct RenoResetCounts { int changed = 0, deleted = 0, skipped = 0; };

class RenovationModel {
 public:
  RenoStatus defaultStatus = RenoStatus::New;

  RenoElement& Add(const std::string& id, const std::string& type, const std::string& host = std::string(), bool annotation = false, double quantity = 0) {
    RenoElement e; e.id = id; e.type = type; e.host = host; e.annotation = annotation; e.quantity = quantity; e.status = defaultStatus;
    els_.push_back(e);
    return els_.back();
  }
  const RenoElement* Find(const std::string& id) const { for (auto& e : els_) if (e.id == id) return &e; return nullptr; }
  const std::vector<RenoElement>& Elements() const { return els_; }

  // Sets the status of every listed element (unknown ids are ignored). Openings hosted by a changed wall take the wall's new status by
  // default. Returns the number of elements whose status was set directly.
  int SetStatus(const std::vector<std::string>& ids, RenoStatus s, bool openingsFollow = true) {
    int n = 0;
    for (auto& id : ids) {
      RenoElement* e = Mut(id);
      if (!e) continue;
      e->status = s; ++n;
      if (openingsFollow) for (auto& o : els_) if (o.host == id) o.status = s;
    }
    return n;
  }
  bool Pin(const std::string& id, const std::string& filter) { RenoElement* e = Mut(id); if (!e || !Filter(filter)) return false; e->pinned = filter; return true; }

  // filters
  bool AddFilter(const RenoFilter& f) { if (f.name.empty() || Filter(f.name)) return false; filters_.push_back(f); return true; }
  const RenoFilter* Filter(const std::string& n) const { for (auto& f : filters_) if (f.name == n) return &f; return nullptr; }
  bool Duplicate(const std::string& from, const std::string& to) {
    const RenoFilter* f = Filter(from);
    if (!f || to.empty() || Filter(to)) return false;
    RenoFilter c = *f; c.name = to; filters_.push_back(c);
    return true;
  }
  // Renaming follows the references in views and pins.
  bool Rename(const std::string& from, const std::string& to) {
    RenoFilter* f = MutFilter(from);
    if (!f || to.empty() || Filter(to)) return false;
    f->name = to;
    for (auto& kv : viewFilter_) if (kv.second == from) kv.second = to;
    for (auto& e : els_) if (e.pinned == from) e.pinned = to;
    return true;
  }
  // Deleting a filter unpins its elements and leaves the views that used it without a filter (assumption).
  bool Delete(const std::string& n) {
    for (auto it = filters_.begin(); it != filters_.end(); ++it) if (it->name == n) {
      filters_.erase(it);
      for (auto& kv : viewFilter_) if (kv.second == n) kv.second.clear();
      for (auto& e : els_) if (e.pinned == n) e.pinned.clear();
      return true;
    }
    return false;
  }
  const std::vector<RenoFilter>& Filters() const { return filters_; }

  // The active filter of each view is independent of the other views. A view without one shows everything normally.
  bool SetViewFilter(const std::string& view, const std::string& filter) { if (!Filter(filter)) return false; viewFilter_[view] = filter; return true; }
  std::string ViewFilter(const std::string& view) const { auto it = viewFilter_.find(view); return it == viewFilter_.end() ? std::string() : it->second; }
  RenoLook LookInView(const std::string& id, const std::string& view) const {
    const RenoElement* e = Find(id);
    const RenoFilter* f = Filter(ViewFilter(view));
    if (!e) return RenoLook::Hidden;
    if (!f) return RenoLook::Normal;
    return LookUnder(*e, *f);
  }
  // Ids shown (normal or overridden) in a view, in model order.
  std::vector<std::string> VisibleIn(const std::string& view) const {
    std::vector<std::string> v;
    for (auto& e : els_) if (LookInView(e.id, view) != RenoLook::Hidden) v.push_back(e.id);
    return v;
  }
  // Schedule records of a view: only the elements its filter shows, with Id, Type, Status and Quantity.
  std::vector<Record> ScheduleRecords(const std::string& view, const std::string& type = std::string()) const {
    static const char* names[] = {"Existing", "Demolished", "New"};
    std::vector<Record> out;
    for (auto& e : els_) {
      if (!type.empty() && e.type != type) continue;
      if (LookInView(e.id, view) == RenoLook::Hidden) continue;
      Record r;
      r["Id"] = Value::Of(e.id); r["Type"] = Value::Of(e.type); r["Status"] = Value::Of(names[(int)e.status]); r["Quantity"] = Value::Of(e.quantity);
      out.push_back(r);
    }
    return out;
  }
  // End-of-renovation reset: remaps statuses (or deletes elements). An element pinned to a filter is handled only when it is visible
  // under the target filter; otherwise it is skipped.
  RenoResetCounts Reset(const RenoRemap& map, const std::string& targetFilter) {
    RenoResetCounts c;
    const RenoFilter* tf = Filter(targetFilter);
    std::vector<RenoElement> keep;
    for (auto& e : els_) {
      int t = map.to[(int)e.status];
      bool visibleInTarget = !tf || LookUnder(e, *tf) != RenoLook::Hidden;
      if (!e.pinned.empty() && !visibleInTarget) { ++c.skipped; keep.push_back(e); continue; }
      if (t == -1) { ++c.deleted; continue; }
      if (t >= 0 && t != (int)e.status) { e.status = (RenoStatus)t; ++c.changed; }
      keep.push_back(e);
    }
    els_ = keep;
    return c;
  }

 private:
  RenoElement* Mut(const std::string& id) { for (auto& e : els_) if (e.id == id) return &e; return nullptr; }
  RenoFilter* MutFilter(const std::string& n) { for (auto& f : filters_) if (f.name == n) return &f; return nullptr; }
  std::vector<RenoElement> els_;
  std::vector<RenoFilter> filters_;
  std::map<std::string, std::string> viewFilter_;
};

inline std::string ExportRenoFiltersXml(const std::vector<RenoFilter>& fs) {
  static const char* sh[] = {"show", "hide", "override"};
  static const char* st[] = {"existing", "demolished", "new"};
  std::string o = "<RenovationFilters>\n";
  for (auto& f : fs) {
    o += "<Filter name=\"" + detail::XmlEsc(f.name) + "\" annotations=\"" + (f.excludeAnnotations ? "excluded" : "included") + "\"";
    for (int i = 0; i < 3; ++i) o += std::string(" ") + st[i] + "=\"" + sh[(int)f.show[i]] + "\" rule_" + st[i] + "=\"" + detail::XmlEsc(f.rule[i]) + "\"";
    o += "/>\n";
  }
  return o + "</RenovationFilters>\n";
}

namespace detail {
struct XmlTag { std::string name; std::map<std::string, std::string> attrs; };
// Self-closing tags "<Name a="v" .../>" in document order. Returns false on an unterminated tag or attribute.
inline bool XmlTags(const std::string& xml, std::vector<XmlTag>& out) {
  out.clear();
  size_t p = 0;
  for (;;) {
    p = xml.find('<', p);
    if (p == std::string::npos) return true;
    if (p + 1 < xml.size() && xml[p + 1] == '/') { p += 2; continue; }
    size_t e = p + 1;
    while (e < xml.size() && xml[e] != ' ' && xml[e] != '>' && xml[e] != '/') ++e;
    XmlTag t; t.name = xml.substr(p + 1, e - p - 1);
    size_t q = e;
    bool selfClosing = false;
    for (;;) {
      while (q < xml.size() && (xml[q] == ' ' || xml[q] == '\n')) ++q;
      if (q >= xml.size()) return false;
      if (xml[q] == '/' && q + 1 < xml.size() && xml[q + 1] == '>') { q += 2; selfClosing = true; break; }
      if (xml[q] == '>') { ++q; break; }
      size_t eq = xml.find("=\"", q);
      if (eq == std::string::npos) return false;
      size_t close = xml.find('"', eq + 2);
      if (close == std::string::npos) return false;
      t.attrs[xml.substr(q, eq - q)] = XmlUnesc(xml.substr(eq + 2, close - eq - 2));
      q = close + 1;
    }
    (void)selfClosing;
    out.push_back(t);
    p = q;
  }
}
}  // namespace detail

inline bool ImportRenoFiltersXml(const std::string& xml, std::vector<RenoFilter>& out) {
  std::vector<detail::XmlTag> tags;
  if (xml.find("<RenovationFilters>") == std::string::npos || xml.find("</RenovationFilters>") == std::string::npos || !detail::XmlTags(xml, tags)) return false;
  static const char* st[] = {"existing", "demolished", "new"};
  std::vector<RenoFilter> fs;
  for (auto& t : tags) {
    if (t.name != "Filter") continue;
    RenoFilter f;
    f.name = t.attrs["name"];
    if (f.name.empty()) return false;
    f.excludeAnnotations = t.attrs["annotations"] != "included";
    for (int i = 0; i < 3; ++i) {
      std::string s = t.attrs[st[i]];
      if (s == "show") f.show[i] = RenoShow::Show; else if (s == "hide") f.show[i] = RenoShow::Hide; else if (s == "override") f.show[i] = RenoShow::Override; else return false;
      f.rule[i] = t.attrs[std::string("rule_") + st[i]];
    }
    fs.push_back(f);
  }
  out = fs;
  return true;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Schedule schemes as XML
// ------------------------------------------------------------------------------------------------------------------------------
struct SchemeHeader { std::string id, name, type; };

namespace detail {
inline std::string NumText(double v) { return FormatNumber(v, 17); }
inline std::string Flag(bool b) { return b ? "1" : "0"; }
}  // namespace detail

// Every Field member and every ScheduleDef member that describes the scheme is written. The floor-plan selection (selectedOnly,
// selection) is transient session state and is left out on purpose, as a saved view leaves it out (SaveAsView).
inline std::string ExportSchemeXml(const ScheduleDef& d, const SchemeHeader& h) {
  using detail::XmlEsc;
  auto fl = [](bool b) { return std::string(b ? "1" : "0"); };
  std::string o = "<Scheme id=\"" + XmlEsc(h.id) + "\" name=\"" + XmlEsc(h.name) + "\" type=\"" + XmlEsc(h.type) + "\" itemize=\"" + fl(d.itemize) + "\" grandTotal=\"" + fl(d.grandTotal) +
                  "\" rounded=\"" + fl(d.roundedGrandTotals) + "\" keepTogether=\"" + XmlEsc(d.keepTogetherBy) + "\" filterBySheet=\"" + fl(d.filterBySheet) + "\" sheet=\"" + XmlEsc(d.sheet) +
                  "\" idField=\"" + XmlEsc(d.idField) + "\">\n";
  for (auto& f : d.fields) {
    o += "<Field name=\"" + XmlEsc(f.name) + "\" kind=\"" + std::to_string((int)f.kind) + "\" formula=\"" + XmlEsc(f.formula) + "\" of=\"" + XmlEsc(f.percentOf) + "\" by=\"" + XmlEsc(f.percentBy) +
         "\" sep=\"" + XmlEsc(f.separator) + "\" aggregate=\"" + std::to_string((int)f.aggregate) + "\" decimals=\"" + std::to_string(f.decimals) + "\" scale=\"" + detail::NumText(f.scale) +
         "\" unit=\"" + XmlEsc(f.unit) + "\" showUnit=\"" + fl(f.showUnit) + "\" mergeAny=\"" + fl(f.mergeAny) + "\" mergeShow=\"" + std::to_string((int)f.mergeShow) +
         "\" mergeQty=\"" + fl(f.mergeAppendQuantity) + "\" listSep=\"" + XmlEsc(f.listSeparator) +
         "\" zeroValueText=\"" + fl(f.zeroValueText) + "\" zeroValue=\"" + XmlEsc(f.zeroValue) + "\" zeroTotalText=\"" + fl(f.zeroTotalText) + "\" zeroTotal=\"" + XmlEsc(f.zeroTotal) +
         "\" zeroGrandText=\"" + fl(f.zeroGrandText) + "\" zeroGrand=\"" + XmlEsc(f.zeroGrand) + "\" notEvaluated=\"" + XmlEsc(f.notEvaluated) + "\" minMaxSep=\"" + XmlEsc(f.minMaxSeparator) +
         "\" totalPrefix=\"" + XmlEsc(f.totalPrefix) + "\" totalSuffix=\"" + XmlEsc(f.totalSuffix) + "\" hidden=\"" + fl(f.hidden) + "\"/>\n";
    for (auto& p : f.parts) o += "<Part field=\"" + XmlEsc(p.field) + "\" prefix=\"" + XmlEsc(p.prefix) + "\" suffix=\"" + XmlEsc(p.suffix) + "\" decimals=\"" + std::to_string(p.decimals) + "\"/>\n";
  }
  for (auto& c : d.filters)
    o += "<Criterion field=\"" + XmlEsc(c.field) + "\" op=\"" + std::to_string((int)c.op) + "\" kind=\"" + std::to_string((int)c.value.kind) + "\" value=\"" +
         XmlEsc(c.value.kind == Value::Num ? detail::NumText(c.value.n) : c.value.s) + "\"/>\n";
  for (auto& k : d.sort)
    o += "<Sort field=\"" + XmlEsc(k.field) + "\" descending=\"" + fl(k.descending) + "\" header=\"" + fl(k.header) + "\" footer=\"" + fl(k.footer) + "\" blank=\"" + fl(k.blankAfter) + "\"/>\n";
  return o + "</Scheme>\n";
}

// An attribute that is absent keeps the member's default (so a file written before a setting existed still loads); a malformed number is
// an error.
inline bool ImportSchemeXml(const std::string& xml, ScheduleDef& d, SchemeHeader& h) {
  std::vector<detail::XmlTag> tags;
  if (xml.find("<Scheme ") == std::string::npos || xml.find("</Scheme>") == std::string::npos || !detail::XmlTags(xml, tags)) return false;
  ScheduleDef out;
  SchemeHeader hh;
  bool seen = false;
  try {
    for (auto& t : tags) {
      const auto& a = t.attrs;
      auto str = [&](const char* k, const std::string& dflt) { auto it = a.find(k); return it == a.end() ? dflt : it->second; };
      auto flag = [&](const char* k, bool dflt) { auto it = a.find(k); return it == a.end() ? dflt : it->second == "1"; };
      auto num = [&](const char* k, double dflt) { auto it = a.find(k); return it == a.end() ? dflt : std::stod(it->second); };
      auto integer = [&](const char* k, int dflt) { auto it = a.find(k); return it == a.end() ? dflt : std::stoi(it->second); };
      if (t.name == "Scheme") {
        seen = true;
        hh.id = str("id", ""); hh.name = str("name", ""); hh.type = str("type", "");
        out.itemize = flag("itemize", out.itemize); out.grandTotal = flag("grandTotal", out.grandTotal); out.roundedGrandTotals = flag("rounded", out.roundedGrandTotals);
        out.keepTogetherBy = str("keepTogether", out.keepTogetherBy); out.filterBySheet = flag("filterBySheet", out.filterBySheet); out.sheet = str("sheet", out.sheet);
        out.idField = str("idField", out.idField);
      } else if (t.name == "Field") {
        Field f;
        f.name = str("name", ""); f.kind = (FieldKind)integer("kind", (int)f.kind); f.formula = str("formula", ""); f.percentOf = str("of", ""); f.percentBy = str("by", "");
        f.separator = str("sep", ""); f.aggregate = (Aggregate)integer("aggregate", (int)f.aggregate); f.decimals = integer("decimals", f.decimals); f.scale = num("scale", f.scale);
        f.unit = str("unit", ""); f.showUnit = flag("showUnit", f.showUnit); f.mergeAny = flag("mergeAny", f.mergeAny); f.mergeShow = (MergeShow)integer("mergeShow", (int)f.mergeShow);
        f.mergeAppendQuantity = flag("mergeQty", f.mergeAppendQuantity); f.listSeparator = str("listSep", f.listSeparator);
        f.zeroValueText = flag("zeroValueText", f.zeroValueText); f.zeroValue = str("zeroValue", ""); f.zeroTotalText = flag("zeroTotalText", f.zeroTotalText); f.zeroTotal = str("zeroTotal", "");
        f.zeroGrandText = flag("zeroGrandText", f.zeroGrandText); f.zeroGrand = str("zeroGrand", ""); f.notEvaluated = str("notEvaluated", f.notEvaluated);
        f.minMaxSeparator = str("minMaxSep", f.minMaxSeparator); f.totalPrefix = str("totalPrefix", ""); f.totalSuffix = str("totalSuffix", ""); f.hidden = flag("hidden", f.hidden);
        out.fields.push_back(f);
      } else if (t.name == "Part") {
        if (out.fields.empty()) return false;
        CombinedPart p; p.field = str("field", ""); p.prefix = str("prefix", ""); p.suffix = str("suffix", ""); p.decimals = integer("decimals", p.decimals);
        out.fields.back().parts.push_back(p);
      } else if (t.name == "Criterion") {
        Condition c; c.field = str("field", ""); c.op = (Op)integer("op", (int)c.op);
        int kind = integer("kind", (int)Value::Undef);
        if (kind == (int)Value::Num) c.value = Value::Of(num("value", 0)); else if (kind == (int)Value::Str) c.value = Value::Of(str("value", ""));
        out.filters.push_back(c);
      } else if (t.name == "Sort") {
        SortKey k; k.field = str("field", ""); k.descending = flag("descending", false); k.header = flag("header", false); k.footer = flag("footer", false); k.blankAfter = flag("blank", false);
        out.sort.push_back(k);
      }
    }
  } catch (...) { return false; }
  if (!seen) return false;
  d = out; h = hh;
  return true;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Field catalog: searchable list and field order
// ------------------------------------------------------------------------------------------------------------------------------
struct CatalogField { std::string name, group; };

inline std::string LowerCase(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// Words of the query must all occur (case-insensitive) in the field's name or group; catalog order is kept. An empty query lists all.
inline std::vector<CatalogField> SearchFields(const std::vector<CatalogField>& cat, const std::string& query) {
  std::vector<std::string> words;
  std::string w;
  for (char c : query + " ") { if (c == ' ') { if (!w.empty()) words.push_back(LowerCase(w)); w.clear(); } else w += c; }
  std::vector<CatalogField> out;
  for (auto& f : cat) {
    std::string hay = LowerCase(f.name + " " + f.group);
    bool all = true;
    for (auto& x : words) if (hay.find(x) == std::string::npos) all = false;
    if (all) out.push_back(f);
  }
  return out;
}

// Moves the field at `index` one place up or down (the up / down controls). Refused at the ends.
inline bool MoveField(std::vector<std::string>& order, size_t index, bool up) {
  if (index >= order.size() || (up && index == 0) || (!up && index + 1 >= order.size())) return false;
  std::swap(order[index], order[up ? index - 1 : index + 1]);
  return true;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Header rows with merged group headers
// ------------------------------------------------------------------------------------------------------------------------------
struct HeaderGroup { std::string label; int first = 0, count = 0; };
struct HeaderCell { std::string text; int span = 1; };
struct HeaderRows { bool ok = true; std::string error; std::vector<std::vector<HeaderCell>> rows; };

// Row 1: the main header spanning all columns (when shown). Row 2: group headers, each spanning its adjacent columns, with empty one-column
// cells between groups (when any group exists). Last row: the field headers (when shown). Groups must lie inside the columns and not overlap.
inline HeaderRows BuildHeaderRows(const std::string& mainHeader, bool showMain, const std::vector<std::string>& fieldHeaders, bool showFields, const std::vector<HeaderGroup>& groups) {
  HeaderRows h;
  int n = (int)fieldHeaders.size();
  std::vector<HeaderGroup> g = groups;
  std::sort(g.begin(), g.end(), [](const HeaderGroup& a, const HeaderGroup& b) { return a.first < b.first; });
  int end = 0;
  for (auto& x : g) {
    if (x.count < 1 || x.first < 0 || x.first + x.count > n) { h.ok = false; h.error = "group outside the columns"; return h; }
    if (x.first < end) { h.ok = false; h.error = "groups overlap"; return h; }
    end = x.first + x.count;
  }
  if (showMain) h.rows.push_back({{mainHeader, n}});
  if (!g.empty()) {
    std::vector<HeaderCell> row;
    int at = 0;
    for (auto& x : g) {
      for (; at < x.first; ++at) row.push_back({"", 1});
      row.push_back({x.label, x.count});
      at = x.first + x.count;
    }
    for (; at < n; ++at) row.push_back({"", 1});
    h.rows.push_back(row);
  }
  if (showFields) { std::vector<HeaderCell> row; for (auto& f : fieldHeaders) row.push_back({f, 1}); h.rows.push_back(row); }
  return h;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Editable schedule cells, overridden values, refresh on reopening
// ------------------------------------------------------------------------------------------------------------------------------
class EditableSchedule {
 public:
  EditableSchedule(std::vector<Record> model, ScheduleDef def, std::set<std::string> editable, std::string idField = "Id")
      : model_(std::move(model)), def_(std::move(def)), editable_(std::move(editable)), idField_(std::move(idField)) {
    for (auto& r : model_) defaults_[IdOf(r)] = r;
  }
  // The schedule as it is when (re)opened: always built from the current model.
  ScheduleResult Open() const { return BuildSchedule(def_, model_); }
  // Editing a cell of data row `row` of an opened schedule changes the model at once. Only editable fields can be edited.
  bool EditCell(const ScheduleResult& opened, size_t row, const std::string& field, const Value& v) {
    if (!editable_.count(field) || row >= opened.rows.size() || opened.rows[row].kind != Row::Data || opened.rows[row].recordIdx.empty()) return false;
    std::string id = IdOf(opened.recs[opened.rows[row].recordIdx[0]]);
    for (auto& r : model_) if (IdOf(r) == id) { r[field] = v; return true; }
    return false;
  }
  // A change made in the model itself (outside the schedule).
  void ModelEdit(const std::string& id, const std::string& field, const Value& v) { for (auto& r : model_) if (IdOf(r) == id) r[field] = v; }
  // Fields of an element whose value differs from its default (the original value).
  std::vector<std::string> Overridden(const std::string& id) const {
    std::vector<std::string> out;
    auto d = defaults_.find(id);
    if (d == defaults_.end()) return out;
    for (auto& r : model_) if (IdOf(r) == id) for (auto& kv : r) if (detail::KeyText(kv.second) != detail::KeyText(Get(d->second, kv.first))) out.push_back(kv.first);
    return out;
  }
  bool Restore(const std::string& id, const std::string& field) {
    auto d = defaults_.find(id);
    if (d == defaults_.end()) return false;
    for (auto& r : model_) if (IdOf(r) == id) { auto v = d->second.find(field); if (v == d->second.end()) r.erase(field); else r[field] = v->second; return true; }
    return false;
  }
  const std::vector<Record>& Model() const { return model_; }
 private:
  std::string IdOf(const Record& r) const { Value v = Get(r, idField_); return v.kind == Value::Str ? v.s : FormatNumber(v.n, -1); }
  std::vector<Record> model_;
  ScheduleDef def_;
  std::set<std::string> editable_;
  std::string idField_;
  std::map<std::string, Record> defaults_;
};

// ------------------------------------------------------------------------------------------------------------------------------
// Length, area and volume formats
// ------------------------------------------------------------------------------------------------------------------------------
enum class LengthUnit { Millimeter, Centimeter, Meter, DecimalFoot, DecimalInch, FeetFractionalInch };
constexpr double kMetersPerFoot = 0.3048;      // exact by definition
constexpr double kMetersPerInch = 0.0254;      // exact by definition

// `denominator` (a power of two) is used by FeetFractionalInch: 3' 11 1/4"
inline std::string FormatLength(double meters, LengthUnit u, int decimals, int denominator = 16) {
  switch (u) {
    case LengthUnit::Millimeter: return FormatNumber(meters * 1000.0, decimals) + " mm";
    case LengthUnit::Centimeter: return FormatNumber(meters * 100.0, decimals) + " cm";
    case LengthUnit::Meter: return FormatNumber(meters, decimals) + " m";
    case LengthUnit::DecimalFoot: return FormatNumber(meters / kMetersPerFoot, decimals) + " ft";
    case LengthUnit::DecimalInch: return FormatNumber(meters / kMetersPerInch, decimals) + " in";
    case LengthUnit::FeetFractionalInch: {
      bool neg = meters < 0;
      double inches = std::fabs(meters) / kMetersPerInch;
      long long units = std::llround(inches * denominator);   // sixteenths (or the chosen denominator) of an inch
      long long whole = units / denominator, num = units % denominator;
      long long feet = whole / 12, in = whole % 12;
      int den = denominator;
      while (num && num % 2 == 0 && den % 2 == 0) { num /= 2; den /= 2; }
      std::string s = (neg ? "-" : "") + std::to_string(feet) + "' " + std::to_string(in);
      if (num) s += " " + std::to_string(num) + "/" + std::to_string(den);
      return s + "\"";
    }
  }
  return std::string();
}
enum class AreaUnit { SquareMeter, SquareFoot, SquareMillimeter };
inline std::string FormatArea(double sqm, AreaUnit u, int decimals) {
  if (u == AreaUnit::SquareMeter) return FormatNumber(sqm, decimals) + " m2";
  if (u == AreaUnit::SquareMillimeter) return FormatNumber(sqm * 1e6, decimals) + " mm2";
  return FormatNumber(sqm / (kMetersPerFoot * kMetersPerFoot), decimals) + " ft2";
}
enum class VolumeUnit { CubicMeter, CubicFoot };
inline std::string FormatVolume(double cum, VolumeUnit u, int decimals) {
  if (u == VolumeUnit::CubicMeter) return FormatNumber(cum, decimals) + " m3";
  return FormatNumber(cum / (kMetersPerFoot * kMetersPerFoot * kMetersPerFoot), decimals) + " ft3";
}
// Editable dimensions follow the dimension preferences, calculated values the calculation-unit settings.
struct UnitSettings { LengthUnit dimension = LengthUnit::Millimeter, calculation = LengthUnit::Meter; int dimDecimals = 0, calcDecimals = 2; };
inline std::string FormatByKind(double meters, bool editableDimension, const UnitSettings& s) {
  return editableDimension ? FormatLength(meters, s.dimension, s.dimDecimals) : FormatLength(meters, s.calculation, s.calcDecimals);
}

// ------------------------------------------------------------------------------------------------------------------------------
// Property sheet: export for consultants, preview and import
// ------------------------------------------------------------------------------------------------------------------------------
using Sheet = std::vector<std::vector<std::string>>;

inline std::string CellText(const Value& v) { return v.kind == Value::Num ? FormatNumber(v.n, -1) : v.kind == Value::Str ? v.s : std::string(); }

// Header row (the id column, then the properties) followed by one row per element.
inline Sheet ExportPropertySheet(const std::vector<Record>& model, const std::string& idField, const std::vector<std::string>& props) {
  Sheet s;
  std::vector<std::string> head = {idField};
  for (auto& p : props) head.push_back(p);
  s.push_back(head);
  for (auto& r : model) {
    std::vector<std::string> row = {CellText(Get(r, idField))};
    for (auto& p : props) row.push_back(CellText(Get(r, p)));
    s.push_back(row);
  }
  return s;
}

struct PropChange { std::string id, prop, oldText, newText; };
struct ImportPreview { std::vector<PropChange> changes; std::vector<std::string> unmatched; std::vector<std::string> invalid; };  // invalid: "id/prop"

// Rows whose id is in no element are listed as unmatched. A property that holds a number must receive a number (else "invalid").
// Only the properties in `filter` (all when empty) are considered; a cell equal to the current value is not a change.
inline ImportPreview PreviewImport(const std::vector<Record>& model, const std::string& idField, const Sheet& sheet, const std::set<std::string>& filter) {
  ImportPreview p;
  if (sheet.empty() || sheet[0].empty()) return p;          // no header, no id column
  for (size_t r = 1; r < sheet.size(); ++r) {
    const std::vector<std::string>& row = sheet[r];
    bool blank = true;
    for (auto& c : row) if (!c.empty()) blank = false;
    if (blank) continue;                                    // an empty or all-blank row (a trailing blank line) is ignored
    const Record* rec = nullptr;
    if (!row[0].empty()) for (auto& m : model) if (CellText(Get(m, idField)) == row[0]) rec = &m;
    if (!rec) { p.unmatched.push_back(row[0].empty() ? "row " + std::to_string(r + 1) : row[0]); continue; }
    for (size_t c = 1; c < sheet[0].size() && c < row.size(); ++c) {   // a short row only carries the columns it has, extra cells are ignored
      const std::string& prop = sheet[0][c];
      if (!filter.empty() && !filter.count(prop)) continue;
      Value cur = Get(*rec, prop);
      const std::string& cell = row[c];
      if (CellText(cur) == cell) continue;
      if (cur.kind == Value::Num) {
        try { size_t used = 0; std::stod(cell, &used); if (used != cell.size()) throw 1; } catch (...) { p.invalid.push_back(row[0] + "/" + prop); continue; }
      }
      p.changes.push_back({row[0], prop, CellText(cur), cell});
    }
  }
  return p;
}
inline void ApplyImport(std::vector<Record>& model, const std::string& idField, const std::vector<PropChange>& changes) {
  for (auto& ch : changes)
    for (auto& m : model) if (CellText(Get(m, idField)) == ch.id) {
      Value cur = Get(m, ch.prop);
      if (cur.kind != Value::Num) { m[ch.prop] = Value::Of(ch.newText); continue; }
      try { m[ch.prop] = Value::Of(std::stod(ch.newText)); } catch (...) {}   // a change that is not a number is left out
    }
}

// ------------------------------------------------------------------------------------------------------------------------------
// Headline rows, splitting, tag labels, conditional formatting
// ------------------------------------------------------------------------------------------------------------------------------
struct HeadlineOptions {
  std::vector<std::string> fields;    // schedule fields repeated in the headline
  std::string separator = " | ";
  bool headerPrefix = false;          // "Field: value"
  int gapRows = 0;                    // blank lines after each element's data
};

// Text lines: for every data row a headline (the chosen visible fields), then the row's cells joined by a tab, then the gap lines.
inline std::vector<std::string> RenderWithHeadline(const ScheduleDef& def, const ScheduleResult& r, const HeadlineOptions& o) {
  std::vector<std::string> heads = Headings(def), lines;
  for (auto& row : r.rows) {
    if (row.kind != Row::Data) continue;
    std::string head;
    for (auto& f : o.fields) {
      size_t i = 0;
      while (i < heads.size() && heads[i] != f) ++i;
      if (i == heads.size()) continue;
      if (!head.empty()) head += o.separator;
      head += (o.headerPrefix ? f + ": " : std::string()) + row.cells[i];
    }
    lines.push_back(head);
    std::string data;
    for (size_t i = 0; i < row.cells.size(); ++i) { if (i) data += "\t"; data += row.cells[i]; }
    lines.push_back(data);
    for (int g = 0; g < o.gapRows; ++g) lines.push_back("");
  }
  return lines;
}

struct SplitPart { int first = 0, count = 0; };
// Rows of a schedule (without the header) that fit a height: floor((height - title - header) / row), never negative.
inline int RowsThatFit(double height, double title, double header, double row) {
  double avail = height - title - header;
  return avail < row || row <= 0 ? 0 : (int)std::floor(avail / row + 1e-9);
}
// Splits `total` rows into consecutive parts of at most `perPart` rows (a segment break per part). Empty for no rows or perPart < 1.
inline std::vector<SplitPart> SplitRows(int total, int perPart) {
  std::vector<SplitPart> parts;
  if (total < 1 || perPart < 1) return parts;
  for (int at = 0; at < total; at += perPart) parts.push_back({at, std::min(perPart, total - at)});
  return parts;
}
// Parts side by side: how many fit in the sheet width, then the layouts needed (parts per layout, in order).
inline std::vector<int> ArrangeParts(int parts, double partWidth, double gap, double sheetWidth) {
  std::vector<int> layouts;
  int perLayout = partWidth <= 0 ? 0 : (int)std::floor((sheetWidth + gap) / (partWidth + gap) + 1e-9);
  if (perLayout < 1 || parts < 1) return layouts;
  for (int left = parts; left > 0; left -= perLayout) layouts.push_back(std::min(perLayout, left));
  return layouts;
}

struct LabelPart {
  std::string param, prefix, suffix, sample;
  int spacesAfter = 0;
  bool breakAfter = false;
};
// Builds a tag label. A parameter with an empty value contributes nothing (prefix and suffix are dropped too: assumption). With
// `useSample` the sample value stands in for a missing one. A forced break starts a new line after the part. Pieces are separated by
// the part's trailing spaces. With maxChars > 0 a line that would pass the width wraps before the next piece; with betweenParamsOnly a
// piece is never cut, otherwise a piece longer than the width is cut at its spaces.
inline std::vector<std::string> BuildLabel(const std::vector<LabelPart>& parts, const std::map<std::string, std::string>& values, bool useSample = false, size_t maxChars = 0, bool betweenParamsOnly = true) {
  std::vector<std::string> lines;
  std::string cur;
  auto flush = [&]() { while (!cur.empty() && cur.back() == ' ') cur.pop_back(); lines.push_back(cur); cur.clear(); };
  bool pendingBreak = false;
  for (size_t i = 0; i < parts.size(); ++i) {
    auto& p = parts[i];
    auto it = values.find(p.param);
    std::string v = it == values.end() ? std::string() : it->second;
    if (v.empty() && useSample) v = p.sample;
    if (pendingBreak) { flush(); pendingBreak = false; }
    if (v.empty()) { if (p.breakAfter) pendingBreak = true; continue; }
    std::string piece = p.prefix + v + p.suffix;
    std::string gap = std::string(p.spacesAfter, ' ');
    std::vector<std::string> chunks = {piece};
    if (maxChars && !betweenParamsOnly && piece.size() > maxChars) {
      chunks.clear();
      std::string line;
      size_t a = 0;
      while (a <= piece.size()) {
        size_t b = piece.find(' ', a);
        std::string w = piece.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!line.empty() && line.size() + 1 + w.size() > maxChars) { chunks.push_back(line); line = w; } else line += (line.empty() ? "" : " ") + w;
        if (b == std::string::npos) break;
        a = b + 1;
      }
      chunks.push_back(line);
    }
    for (size_t c = 0; c < chunks.size(); ++c) {
      if (maxChars && !cur.empty() && cur.size() + chunks[c].size() > maxChars) { while (!cur.empty() && cur.back() == ' ') cur.pop_back(); flush(); }
      cur += chunks[c];
      if (c + 1 < chunks.size()) flush();
    }
    cur += gap;
    if (p.breakAfter) pendingBreak = true;
  }
  while (!cur.empty() && cur.back() == ' ') cur.pop_back();
  if (!cur.empty() || lines.empty()) lines.push_back(cur);
  return lines;
}

struct CondFormat { Condition cond; std::string background; };
// The first rule that holds sets the background; "" when none does (the rule order is an assumption).
inline std::string ConditionalBackground(const std::vector<CondFormat>& rules, const Record& r) {
  for (auto& f : rules) if (Test(f.cond, r)) return f.background;
  return std::string();
}

}  // namespace bimdoc
}  // namespace kernel
}  // namespace dino8
