// BIM documentation and collaboration logic: schedule engine (formula, percentage and combined fields, conditions, criteria with
// parentheses, sorting, grouping, totals, merge of identical records, zero / not-evaluated text, delimited export), material
// takeoff with mass and embodied carbon, door and window mark numbering, revision numbering and revision schedules, shared-model
// ownership (reserve, borrow, request, grant, send, receive, relinquish) and model comparison.
//
// Header-only and free of any dependency beyond the standard library, so the tests need no geometry kernel.
//
// Assumptions that the cited vendor pages do not spell out (recorded here, and in the notes of the Register rows):
//  * criteria combine with AND binding tighter than OR (standard boolean precedence);
//  * "Under" in a classification test means a strict descendant of the given path;
//  * a merged field in Uniform mode shows nothing when the merged records disagree;
//  * a grant first sends the owner's unsent edits to the element, so the requester has to receive before editing it;
//  * each revision numbering kind (numeric, alphanumeric) counts on its own, in sequence order.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dino8 {
namespace kernel {
namespace bimdoc {

// ------------------------------------------------------------------------------------------------------------------------------
// Values and records
// ------------------------------------------------------------------------------------------------------------------------------
struct Value {
  enum Kind { Undef, Num, Str } kind = Undef;
  double n = 0;
  std::string s;
  static Value Of(double v) { Value r; r.kind = Num; r.n = v; return r; }
  static Value Of(const std::string& v) { Value r; r.kind = Str; r.s = v; return r; }
  static Value Of(const char* v) { return Of(std::string(v)); }
  bool Defined() const { return kind != Undef; }
};
using Record = std::map<std::string, Value>;

inline Value Get(const Record& r, const std::string& name) {
  auto it = r.find(name);
  return it == r.end() ? Value() : it->second;
}

inline double RoundTo(double v, int decimals) {
  double k = std::pow(10.0, decimals);
  return std::round(v * k) / k;
}

// decimals < 0 prints the shortest form with up to 10 significant digits.
inline std::string FormatNumber(double v, int decimals) {
  char buf[64];
  if (decimals < 0) std::snprintf(buf, sizeof buf, "%.10g", v);
  else std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  std::string s = buf;
  if (s == "-0" ) s = "0";
  return s;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Formulas: + - * / ^, parentheses, comparisons, sqrt abs round min max if and or. An unknown identifier, an undefined operand or a
// division by zero gives an undefined result ("not evaluated"); a syntax error returns false.
// ------------------------------------------------------------------------------------------------------------------------------
class Formula {
 public:
  static bool Eval(const std::string& text, const Record& rec, Value& out) {
    Formula f(text, rec);
    f.SkipWs();
    bool ok = f.Cmp(out);
    f.SkipWs();
    return ok && f.p_ == f.t_.size();
  }

 private:
  Formula(const std::string& t, const Record& r) : t_(t), rec_(r) {}
  const std::string& t_;
  const Record& rec_;
  size_t p_ = 0;

  void SkipWs() { while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t')) ++p_; }
  bool Eat(char c) { SkipWs(); if (p_ < t_.size() && t_[p_] == c) { ++p_; return true; } return false; }
  static Value Und() { return Value(); }
  static bool Both(const Value& a, const Value& b) { return a.kind == Value::Num && b.kind == Value::Num; }

  bool Cmp(Value& out) {
    if (!Add(out)) return false;
    SkipWs();
    int op = 0;  // 1 <  2 >  3 <=  4 >=  5 =  6 <>
    if (p_ < t_.size()) {
      char c = t_[p_];
      char d = p_ + 1 < t_.size() ? t_[p_ + 1] : 0;
      if (c == '<' && d == '=') { op = 3; p_ += 2; }
      else if (c == '>' && d == '=') { op = 4; p_ += 2; }
      else if (c == '<' && d == '>') { op = 6; p_ += 2; }
      else if (c == '<') { op = 1; ++p_; }
      else if (c == '>') { op = 2; ++p_; }
      else if (c == '=') { op = 5; ++p_; }
    }
    if (!op) return true;
    Value r;
    if (!Add(r)) return false;
    if (!Both(out, r)) { out = Und(); return true; }
    double a = out.n, b = r.n;
    bool v = op == 1 ? a < b : op == 2 ? a > b : op == 3 ? a <= b : op == 4 ? a >= b : op == 5 ? a == b : a != b;
    out = Value::Of(v ? 1.0 : 0.0);
    return true;
  }
  bool Add(Value& out) {
    if (!Mul(out)) return false;
    for (;;) {
      SkipWs();
      if (p_ >= t_.size() || (t_[p_] != '+' && t_[p_] != '-')) return true;
      char op = t_[p_++];
      Value r;
      if (!Mul(r)) return false;
      out = Both(out, r) ? Value::Of(op == '+' ? out.n + r.n : out.n - r.n) : Und();
    }
  }
  bool Mul(Value& out) {
    if (!Unary(out)) return false;
    for (;;) {
      SkipWs();
      if (p_ >= t_.size() || (t_[p_] != '*' && t_[p_] != '/')) return true;
      char op = t_[p_++];
      Value r;
      if (!Unary(r)) return false;
      if (!Both(out, r)) out = Und();
      else if (op == '*') out = Value::Of(out.n * r.n);
      else out = r.n == 0 ? Und() : Value::Of(out.n / r.n);
    }
  }
  // unary minus binds looser than ^: -2^2 = -4
  bool Unary(Value& out) {
    if (Eat('-')) {
      if (!Unary(out)) return false;
      if (out.kind == Value::Num) out.n = -out.n;
      return true;
    }
    return Pow(out);
  }
  // right associative: 2^3^2 = 2^9
  bool Pow(Value& out) {
    if (!Primary(out)) return false;
    if (Eat('^')) {
      Value r;
      if (!Unary(r)) return false;
      out = Both(out, r) ? Value::Of(std::pow(out.n, r.n)) : Und();
    }
    return true;
  }
  bool Primary(Value& out) {
    SkipWs();
    if (p_ >= t_.size()) return false;
    char c = t_[p_];
    if (c == '(') { ++p_; if (!Cmp(out)) return false; return Eat(')'); }
    if ((c >= '0' && c <= '9') || c == '.') {
      size_t q = p_;
      while (q < t_.size() && ((t_[q] >= '0' && t_[q] <= '9') || t_[q] == '.')) ++q;
      out = Value::Of(std::stod(t_.substr(p_, q - p_)));
      p_ = q;
      return true;
    }
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
      size_t q = p_;
      while (q < t_.size() && ((t_[q] >= 'A' && t_[q] <= 'Z') || (t_[q] >= 'a' && t_[q] <= 'z') || t_[q] == '_' || (t_[q] >= '0' && t_[q] <= '9'))) ++q;
      std::string id = t_.substr(p_, q - p_);
      p_ = q;
      SkipWs();
      if (p_ < t_.size() && t_[p_] == '(') {
        ++p_;
        std::vector<Value> args;
        if (!Eat(')')) {
          for (;;) {
            Value a;
            if (!Cmp(a)) return false;
            args.push_back(a);
            if (Eat(',')) continue;
            if (Eat(')')) break;
            return false;
          }
        }
        return Call(id, args, out);
      }
      out = Get(rec_, id);
      return true;
    }
    return false;
  }
  bool Call(const std::string& f, const std::vector<Value>& a, Value& out) {
    auto allNum = [&] { for (auto& v : a) if (v.kind != Value::Num) return false; return true; };
    if (f == "if") {
      if (a.size() != 3) return false;
      if (a[0].kind != Value::Num) { out = Und(); return true; }
      out = a[0].n != 0 ? a[1] : a[2];
      return true;
    }
    if (f == "sqrt" || f == "abs" || f == "round") {
      if (a.size() != 1) return false;
      if (!allNum()) { out = Und(); return true; }
      out = f == "sqrt" ? (a[0].n < 0 ? Und() : Value::Of(std::sqrt(a[0].n))) : f == "abs" ? Value::Of(std::fabs(a[0].n)) : Value::Of(std::round(a[0].n));
      return true;
    }
    if (f == "min" || f == "max" || f == "and" || f == "or") {
      if (a.size() < 2) return false;
      if (!allNum()) { out = Und(); return true; }
      double r = a[0].n;
      for (size_t i = 1; i < a.size(); ++i) {
        if (f == "min") r = std::min(r, a[i].n);
        else if (f == "max") r = std::max(r, a[i].n);
        else if (f == "and") r = (r != 0 && a[i].n != 0) ? 1 : 0;
        else r = (r != 0 || a[i].n != 0) ? 1 : 0;
      }
      out = Value::Of(r);
      return true;
    }
    return false;
  }
};

// ------------------------------------------------------------------------------------------------------------------------------
// Conditions and criteria
// ------------------------------------------------------------------------------------------------------------------------------
enum class Op { Eq, Ne, Gt, Ge, Lt, Le, Contains, NotContains, BeginsWith, EndsWith, HasValue, HasNoValue, ClassIs, ClassIsNot, ClassUnder, AssocGlobal, NotAssocGlobal };

struct Condition {
  std::string field;
  Op op = Op::Eq;
  Value value;
};

// All comparisons are case-sensitive. A comparison with an undefined field is false (except Ne / NotContains / ClassIsNot,
// which are true when the field is undefined, and HasNoValue).
inline bool Test(const Condition& c, const Record& r) {
  Value v = Get(r, c.field);
  if (c.op == Op::AssocGlobal || c.op == Op::NotAssocGlobal) {
    // a parameter tied to a global parameter carries the global's name in the field "<parameter>@global"
    Value g = Get(r, c.field + "@global");
    bool tied = g.kind == Value::Str && g.s == (c.value.kind == Value::Str ? c.value.s : std::string());
    return c.op == Op::AssocGlobal ? tied : !tied;
  }
  if (c.op == Op::HasValue) return v.Defined();
  if (c.op == Op::HasNoValue) return !v.Defined();
  if (!v.Defined()) return c.op == Op::Ne || c.op == Op::NotContains || c.op == Op::ClassIsNot;
  auto text = [](const Value& x) { return x.kind == Value::Str ? x.s : FormatNumber(x.n, -1); };
  switch (c.op) {
    case Op::Eq: case Op::Ne: {
      bool eq = (v.kind == Value::Num && c.value.kind == Value::Num) ? v.n == c.value.n : text(v) == text(c.value);
      return c.op == Op::Eq ? eq : !eq;
    }
    case Op::Gt: case Op::Ge: case Op::Lt: case Op::Le: {
      int cmp;
      if (v.kind == Value::Num && c.value.kind == Value::Num) cmp = v.n < c.value.n ? -1 : v.n > c.value.n ? 1 : 0;
      else { std::string a = text(v), b = text(c.value); cmp = a < b ? -1 : a > b ? 1 : 0; }
      return c.op == Op::Gt ? cmp > 0 : c.op == Op::Ge ? cmp >= 0 : c.op == Op::Lt ? cmp < 0 : cmp <= 0;
    }
    case Op::Contains: return text(v).find(text(c.value)) != std::string::npos;
    case Op::NotContains: return text(v).find(text(c.value)) == std::string::npos;
    case Op::BeginsWith: return text(v).compare(0, text(c.value).size(), text(c.value)) == 0;
    case Op::EndsWith: { std::string a = text(v), b = text(c.value); return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0; }
    case Op::AssocGlobal: case Op::NotAssocGlobal: return false;  // handled before the undefined check
    case Op::ClassIs: return text(v) == text(c.value);
    case Op::ClassIsNot: return text(v) != text(c.value);
    case Op::ClassUnder: { std::string a = text(v), b = text(c.value) + "/"; return a.size() > b.size() && a.compare(0, b.size(), b) == 0; }
    default: return false;
  }
}

// One row of the Archicad criteria list: leading parentheses, the test, trailing parentheses, and the join to the row above.
struct CriterionRow {
  int open = 0;
  Condition cond;
  int close = 0;
  bool orWithPrevious = false;  // join to the previous row; ignored on the first row
};

struct CriteriaResult {
  bool valid = true;
  std::string warning;
  std::vector<size_t> matches;  // indexes into the records
};

inline bool EvalCriteria(const std::vector<CriterionRow>& rows, const Record& rec, bool& result) {
  // balance check
  int depth = 0;
  for (auto& r : rows) {
    depth += r.open;
    depth -= r.close;
    if (depth < 0) return false;
  }
  if (depth != 0) return false;
  // token stream: 'L' leaf, '(' ')' '&' '|'
  struct Tok { char k; bool v; };
  std::vector<Tok> t;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (i) t.push_back({rows[i].orWithPrevious ? '|' : '&', false});
    for (int k = 0; k < rows[i].open; ++k) t.push_back({'(', false});
    t.push_back({'L', Test(rows[i].cond, rec)});
    for (int k = 0; k < rows[i].close; ++k) t.push_back({')', false});
  }
  size_t p = 0;
  bool bad = false;
  struct P {
    std::vector<Tok>& t; size_t& p; bool& bad;
    bool Or() { bool v = And(); while (!bad && p < t.size() && t[p].k == '|') { ++p; bool r = And(); v = v || r; } return v; }
    bool And() { bool v = Atom(); while (!bad && p < t.size() && t[p].k == '&') { ++p; bool r = Atom(); v = v && r; } return v; }
    bool Atom() {
      if (p >= t.size()) { bad = true; return false; }
      if (t[p].k == 'L') return t[p++].v;
      if (t[p].k == '(') { ++p; bool v = Or(); if (p < t.size() && t[p].k == ')') ++p; else bad = true; return v; }
      bad = true;
      return false;
    }
  } parser{t, p, bad};
  result = parser.Or();
  return !bad && p == t.size();
}

inline CriteriaResult FilterByCriteria(const std::vector<CriterionRow>& rows, const std::vector<Record>& recs) {
  CriteriaResult out;
  if (rows.empty()) {
    for (size_t i = 0; i < recs.size(); ++i) out.matches.push_back(i);
    return out;
  }
  for (size_t i = 0; i < recs.size(); ++i) {
    bool v = false;
    if (!EvalCriteria(rows, recs[i], v)) {
      out.valid = false;
      out.warning = "invalid criteria: unbalanced parentheses";
      out.matches.clear();
      return out;
    }
    if (v) out.matches.push_back(i);
  }
  return out;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Schedule definition and engine
// ------------------------------------------------------------------------------------------------------------------------------
enum class FieldKind { Param, Formula, Percent, Combined, Count };
enum class Aggregate { None, Total, Min, Max, MinMax };
enum class MergeShow { Uniform, Sum, List };

struct CombinedPart {
  std::string field;
  std::string prefix, suffix;
  int decimals = -1;
};

struct Field {
  std::string name;
  FieldKind kind = FieldKind::Param;
  std::string formula;                 // Formula
  std::string percentOf, percentBy;    // Percent: share of `percentOf` within the group of `percentBy` ("" = whole schedule)
  std::vector<CombinedPart> parts;     // Combined
  std::string separator;               // Combined
  Aggregate aggregate = Aggregate::None;
  int decimals = -1;
  double scale = 1;                    // displayed value = stored value * scale
  std::string unit;                    // appended to displayed numbers when showUnit
  bool showUnit = false;
  bool mergeAny = false;               // ignored when deciding whether records are identical
  MergeShow mergeShow = MergeShow::Uniform;
  bool mergeAppendQuantity = true;     // List mode: "0.9 (2)"
  std::string listSeparator = ", ";
  bool zeroValueText = false;    std::string zeroValue;
  bool zeroTotalText = false;    std::string zeroTotal;
  bool zeroGrandText = false;    std::string zeroGrand;
  std::string notEvaluated = "-";
  std::string minMaxSeparator = " - ";   // MinMax shows "min - max" (the display format is an assumption)
  std::string totalPrefix, totalSuffix;
  bool hidden = false;
};

struct SortKey {
  std::string field;
  bool descending = false;
  bool header = false, footer = false, blankAfter = false;
};

struct ScheduleDef {
  std::vector<Field> fields;
  std::vector<Condition> filters;      // at most 8, all must pass
  std::vector<SortKey> sort;
  bool itemize = true;                 // false: identical rows collapse
  bool grandTotal = false;
  bool roundedGrandTotals = false;     // true: grand total adds the displayed (rounded) values
  bool filterBySheet = false;          // keep records whose "Sheets" field ("A101;A102") lists `sheet`
  std::string sheet;
  bool selectedOnly = false;           // keep records whose `idField` is in `selection`
  std::set<std::string> selection;
  std::string idField = "Id";
  std::string keepTogetherBy;          // component schedules: records of one value of this field stay together, in input order
};

// A saved view does not store the selection.
inline ScheduleDef SaveAsView(ScheduleDef d) { d.selectedOnly = false; d.selection.clear(); return d; }

struct Row {
  enum Kind { Data, GroupHeader, GroupFooter, Blank, GrandTotal } kind = Data;
  std::vector<std::string> cells;      // visible fields only
  std::string label;                   // header / footer / grand total title
  int count = 0;                       // records represented by the row
  int level = 0;
  std::vector<size_t> recordIdx;       // Data rows: indexes into ScheduleResult::recs
};

struct ScheduleResult {
  bool ok = true;
  std::string error;
  std::vector<Row> rows;
  std::vector<Record> recs;            // the prepared, filtered and ordered records the rows refer to
};

namespace detail {

inline std::string KeyText(const Value& v) {
  if (v.kind == Value::Num) return "n" + FormatNumber(v.n, 17);
  if (v.kind == Value::Str) return "s" + v.s;
  return "u";
}

inline int CompareValues(const Value& a, const Value& b) {
  if (a.kind != b.kind) return a.kind < b.kind ? -1 : 1;  // Undef < Num < Str
  if (a.kind == Value::Num) return a.n < b.n ? -1 : a.n > b.n ? 1 : 0;
  if (a.kind == Value::Str) return a.s < b.s ? -1 : a.s > b.s ? 1 : 0;
  return 0;
}

inline std::string Plain(const Value& v, const Field& f) {
  if (!v.Defined()) return f.notEvaluated;
  if (v.kind == Value::Str) return v.s;
  double d = v.n * f.scale;
  std::string s;
  if (f.zeroValueText && RoundTo(d, f.decimals < 0 ? 10 : f.decimals) == 0) return f.zeroValue;
  s = FormatNumber(d, f.decimals);
  if (f.showUnit) s += f.unit;
  return s;
}

inline std::string TotalText(double d, const Field& f, bool grand) {
  double shown = d * f.scale;
  bool zero = RoundTo(shown, f.decimals < 0 ? 10 : f.decimals) == 0;
  if (zero && grand && f.zeroGrandText) return f.zeroGrand;
  if (zero && !grand && f.zeroTotalText) return f.zeroTotal;
  std::string s = f.totalPrefix + FormatNumber(shown, f.decimals);
  if (f.showUnit) s += f.unit;
  return s + f.totalSuffix;
}

}  // namespace detail

inline const Field* FindField(const ScheduleDef& d, const std::string& n) {
  for (auto& f : d.fields) if (f.name == n) return &f;
  return nullptr;
}

// Evaluates calculated, percentage and combined fields into the records (a copy is used).
//
// Pass 0 walks the fields in declaration order and evaluates every Formula and Combined field exactly once, so a formula sees the
// Combined fields declared before it and its own name, when the record already holds one, is read as the input value and replaced once.
// Expression-defined properties may also use formula fields declared LATER: a formula field that came out not evaluated, whose name the
// record did not hold beforehand, is tried again, in field order, until a whole sweep defines nothing new. In those retries the
// Combined fields declared after the formula stay invisible, as in pass 0. A value that depends on itself (directly or through other
// fields) never gets a defined operand and stays not evaluated. `evaluations` (optional) counts the formula evaluations.
inline std::vector<Record> Prepare(const ScheduleDef& def, std::vector<Record> recs, int* evaluations = nullptr) {
  const size_t nf = def.fields.size();
  std::vector<std::set<std::string>> held(recs.size());   // keys each record held before any field was computed
  for (size_t i = 0; i < recs.size(); ++i) for (auto& kv : recs[i]) held[i].insert(kv.first);
  auto eval = [&](const Field& f, const Record& view) {
    if (evaluations) ++*evaluations;
    Value v;
    if (!Formula::Eval(f.formula, view, v)) v = Value();
    return v;
  };
  for (auto& f : def.fields) {
    if (f.kind == FieldKind::Formula) {
      for (auto& r : recs) r[f.name] = eval(f, r);
    } else if (f.kind == FieldKind::Combined) {
      for (auto& r : recs) {
        std::string out;
        bool first = true;
        for (auto& p : f.parts) {
          Value v = Get(r, p.field);
          std::string t = v.kind == Value::Num ? FormatNumber(v.n, p.decimals) : v.s;
          if (!first) out += f.separator;
          out += p.prefix + t + p.suffix;
          first = false;
        }
        r[f.name] = Value::Of(out);
      }
    }
  }
  for (bool progress = true; progress;) {
    progress = false;
    for (size_t fi = 0; fi < nf; ++fi) {
      const Field& f = def.fields[fi];
      if (f.kind != FieldKind::Formula) continue;
      for (size_t ri = 0; ri < recs.size(); ++ri) {
        Record& r = recs[ri];
        if (held[ri].count(f.name) || r[f.name].Defined()) continue;
        Record view = r;
        for (size_t j = fi + 1; j < nf; ++j)
          if (def.fields[j].kind == FieldKind::Combined && !held[ri].count(def.fields[j].name)) view.erase(def.fields[j].name);
        Value v = eval(f, view);
        if (v.Defined()) { r[f.name] = v; progress = true; }
      }
    }
  }
  return recs;
}

inline ScheduleResult BuildSchedule(const ScheduleDef& def, const std::vector<Record>& input) {
  ScheduleResult res;
  if (def.filters.size() > 8) { res.ok = false; res.error = "at most 8 filters"; return res; }
  std::vector<Record> all = Prepare(def, input);
  std::vector<Record> recs;
  for (auto& r : all) {
    bool pass = true;
    for (auto& c : def.filters) if (!Test(c, r)) { pass = false; break; }
    if (pass && def.filterBySheet) {
      Value sh = Get(r, "Sheets");
      std::string list = ";" + (sh.kind == Value::Str ? sh.s : std::string()) + ";";
      pass = list.find(";" + def.sheet + ";") != std::string::npos;
    }
    if (pass && def.selectedOnly) {
      Value id = Get(r, def.idField);
      pass = def.selection.count(id.kind == Value::Str ? id.s : FormatNumber(id.n, -1)) > 0;
    }
    if (pass) recs.push_back(r);
  }
  // percentage fields over the filtered set
  for (auto& f : def.fields) {
    if (f.kind != FieldKind::Percent) continue;
    std::map<std::string, double> sum;
    auto gk = [&](const Record& r) { return f.percentBy.empty() ? std::string() : detail::KeyText(Get(r, f.percentBy)); };
    for (auto& r : recs) { Value v = Get(r, f.percentOf); if (v.kind == Value::Num) sum[gk(r)] += v.n; }
    for (auto& r : recs) {
      Value v = Get(r, f.percentOf);
      double s = sum[gk(r)];
      r[f.name] = (v.kind == Value::Num && s != 0) ? Value::Of(100.0 * v.n / s) : Value();
    }
  }
  // sort
  auto less = [&](const Record& a, const Record& b) {
    for (auto& k : def.sort) {
      int c = detail::CompareValues(Get(a, k.field), Get(b, k.field));
      if (c) return k.descending ? c > 0 : c < 0;
    }
    return false;
  };
  if (def.keepTogetherBy.empty()) {
    std::stable_sort(recs.begin(), recs.end(), less);
  } else {
    // elements ordered by the sort keys of their first component; components keep their input order
    std::vector<std::string> keys;
    std::map<std::string, std::vector<Record>> byKey;
    for (auto& r : recs) {
      std::string k = detail::KeyText(Get(r, def.keepTogetherBy));
      if (!byKey.count(k)) keys.push_back(k);
      byKey[k].push_back(r);
    }
    std::stable_sort(keys.begin(), keys.end(), [&](const std::string& a, const std::string& b) { return less(byKey[a][0], byKey[b][0]); });
    std::vector<Record> out;
    for (auto& k : keys) for (auto& r : byKey[k]) out.push_back(r);
    recs.swap(out);
  }
  // rows: a row is a list of record indexes
  std::vector<std::vector<size_t>> rowRecs;
  if (def.itemize) {
    for (size_t i = 0; i < recs.size(); ++i) rowRecs.push_back({i});
  } else {
    std::map<std::string, size_t> where;
    for (size_t i = 0; i < recs.size(); ++i) {
      std::string key;
      for (auto& f : def.fields) {
        if (f.kind == FieldKind::Count || f.mergeAny) continue;
        key += detail::KeyText(Get(recs[i], f.name)) + "\x1f";
      }
      // identical rows are only merged when adjacent in sort order or equal as a whole: use the global key
      auto it = where.find(key);
      if (it == where.end()) { where[key] = rowRecs.size(); rowRecs.push_back({i}); }
      else rowRecs[it->second].push_back(i);
    }
  }
  auto cellsFor = [&](const std::vector<size_t>& idx) {
    std::vector<std::string> cells;
    for (auto& f : def.fields) {
      if (f.hidden) continue;
      if (f.kind == FieldKind::Count) { cells.push_back(std::to_string(idx.size())); continue; }
      if (idx.size() == 1) { cells.push_back(detail::Plain(Get(recs[idx[0]], f.name), f)); continue; }
      // merged row
      std::vector<Value> vs;
      for (size_t i : idx) vs.push_back(Get(recs[i], f.name));
      bool uniform = true;
      for (auto& v : vs) if (detail::KeyText(v) != detail::KeyText(vs[0])) uniform = false;
      if (f.mergeShow == MergeShow::Sum && vs[0].kind == Value::Num) {
        double s = 0;
        bool undef = false;
        for (auto& v : vs) { if (v.kind == Value::Num) s += v.n; else undef = true; }
        cells.push_back(undef ? f.notEvaluated : detail::Plain(Value::Of(s), f));
      } else if (f.mergeShow == MergeShow::List) {
        std::vector<std::string> order;
        std::map<std::string, int> n;
        for (auto& v : vs) { std::string t = detail::Plain(v, f); if (!n.count(t)) order.push_back(t); ++n[t]; }
        std::string out;
        for (size_t k = 0; k < order.size(); ++k) {
          if (k) out += f.listSeparator;
          out += order[k];
          if (f.mergeAppendQuantity) out += " (" + std::to_string(n[order[k]]) + ")";
        }
        cells.push_back(out);
      } else {
        cells.push_back(uniform ? detail::Plain(vs[0], f) : std::string());
      }
    }
    return cells;
  };
  // aggregate cells for a set of records (group footers and grand total)
  auto aggCells = [&](const std::vector<size_t>& rs, bool grand) {
    std::vector<std::string> cells;
    for (auto& f : def.fields) {
      if (f.hidden) continue;
      if (f.aggregate == Aggregate::None || (f.kind == FieldKind::Count)) { cells.push_back(""); continue; }
      bool any = false;
      double acc = 0, lo = 0, hi = 0;
      for (size_t i : rs) {
        Value v = Get(recs[i], f.name);
        if (v.kind != Value::Num) continue;
        double x = v.n;
        if (!any) lo = hi = x; else { lo = std::min(lo, x); hi = std::max(hi, x); }
        if (f.aggregate == Aggregate::Total && grand && def.roundedGrandTotals && f.decimals >= 0) x = RoundTo(x * f.scale, f.decimals) / (f.scale == 0 ? 1 : f.scale);
        if (!any) acc = x;
        else if (f.aggregate == Aggregate::Total) acc += x;
        else if (f.aggregate == Aggregate::Min) acc = std::min(acc, x);
        else acc = std::max(acc, x);
        any = true;
      }
      if (any && f.aggregate == Aggregate::MinMax) { cells.push_back(detail::TotalText(lo, f, grand) + f.minMaxSeparator + detail::TotalText(hi, f, grand)); continue; }
      cells.push_back(any ? detail::TotalText(acc, f, grand) : f.notEvaluated);
    }
    return cells;
  };
  auto keyAt = [&](size_t recIdx, size_t lvl) { return detail::KeyText(Get(recs[recIdx], def.sort[lvl].field)); };
  auto labelAt = [&](size_t recIdx, size_t lvl) {
    const Field* f = FindField(def, def.sort[lvl].field);
    Field tmp;
    return detail::Plain(Get(recs[recIdx], def.sort[lvl].field), f ? *f : tmp);
  };
  // emit rows with group headers / footers / blanks
  size_t L = def.sort.size();
  std::vector<std::vector<size_t>> openRecs(L);  // records of the currently open group at each level
  std::vector<std::string> openKey(L);
  std::vector<size_t> openFirst(L, 0);
  auto closeLevel = [&](size_t lvl) {
    if (def.sort[lvl].footer) {
      Row r; r.kind = Row::GroupFooter; r.level = (int)lvl; r.label = labelAt(openFirst[lvl], lvl);
      r.count = (int)openRecs[lvl].size();
      r.cells = aggCells(openRecs[lvl], false);
      res.rows.push_back(r);
    }
    if (def.sort[lvl].blankAfter) { Row b; b.kind = Row::Blank; b.level = (int)lvl; res.rows.push_back(b); }
    openRecs[lvl].clear();
    openKey[lvl].clear();
  };
  bool started = false;
  for (auto& idx : rowRecs) {
    size_t first = idx[0];
    // first level whose key changed
    size_t change = L;
    if (!started) change = 0;
    else for (size_t l = 0; l < L; ++l) if (keyAt(first, l) != openKey[l]) { change = l; break; }
    if (started && L) for (size_t l = L; l-- > change;) if (!openRecs[l].empty()) closeLevel(l);
    for (size_t l = change; l < L; ++l) {
      openKey[l] = keyAt(first, l);
      openFirst[l] = first;
      if (def.sort[l].header) { Row h; h.kind = Row::GroupHeader; h.level = (int)l; h.label = labelAt(first, l); res.rows.push_back(h); }
    }
    started = true;
    Row r; r.kind = Row::Data; r.cells = cellsFor(idx); r.count = (int)idx.size(); r.recordIdx = idx;
    res.rows.push_back(r);
    for (size_t l = 0; l < L; ++l) for (size_t i : idx) openRecs[l].push_back(i);
  }
  if (started) for (size_t l = L; l-- > 0;) if (!openRecs[l].empty()) closeLevel(l);
  if (def.grandTotal) {
    std::vector<size_t> everything;
    for (size_t i = 0; i < recs.size(); ++i) everything.push_back(i);
    Row g; g.kind = Row::GrandTotal; g.label = "Grand total"; g.count = (int)recs.size();
    g.cells = aggCells(everything, true);
    res.rows.push_back(g);
  }
  res.recs = recs;
  return res;
}

// Headings of the visible fields.
inline std::vector<std::string> Headings(const ScheduleDef& d) {
  std::vector<std::string> h;
  for (auto& f : d.fields) if (!f.hidden) h.push_back(f.name);
  return h;
}

struct ExportOptions {
  bool headerRow = true;
  bool groupRows = false;   // group headers, footers and blank rows
  char delimiter = '\t';
  char qualifier = '"';     // 0: none
};

inline std::string ExportDelimited(const ScheduleDef& def, const ScheduleResult& res, const ExportOptions& o) {
  auto quote = [&](const std::string& s) {
    if (!o.qualifier) return s;
    std::string out(1, o.qualifier);
    for (char c : s) { if (c == o.qualifier) out += c; out += c; }
    return out + o.qualifier;
  };
  auto line = [&](const std::vector<std::string>& cells) {
    std::string s;
    for (size_t i = 0; i < cells.size(); ++i) { if (i) s += o.delimiter; s += quote(cells[i]); }
    return s + "\n";
  };
  std::string out;
  if (o.headerRow) out += line(Headings(def));
  size_t nf = Headings(def).size();
  for (auto& r : res.rows) {
    if (r.kind == Row::Data) { out += line(r.cells); continue; }
    if (r.kind == Row::GrandTotal) { std::vector<std::string> c = r.cells; if (!c.empty() && c[0].empty()) c[0] = r.label; out += line(c); continue; }
    if (!o.groupRows) continue;
    std::vector<std::string> c(nf);
    if (r.kind == Row::GroupHeader) { if (nf) c[0] = r.label; }
    else if (r.kind == Row::GroupFooter) { c = r.cells; if (nf && c[0].empty()) c[0] = r.label + ": " + std::to_string(r.count); }
    out += line(c);
  }
  return out;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Material takeoff (per layer, per material) with mass and embodied indicators
// ------------------------------------------------------------------------------------------------------------------------------
struct MaterialDef {
  std::string name;
  double density = 0;            // kg per m3
  bool hasCarbon = false;  double carbonPerKg = 0;   // kg CO2e per kg
  bool hasEnergy = false;  double energyPerKg = 0;   // MJ per kg
  double costPerM3 = 0;
};
struct LayerDef { std::string material; double thickness = 0; };
struct LayeredElement {
  std::string id, category;
  double faceArea = 0;           // m2 of one face (opening areas already removed or listed below)
  double openingArea = 0;        // m2 removed from every layer
  std::vector<LayerDef> layers;
};

// One record per element layer: Element, Category, Material, Area, Thickness, Volume, Cost, Mass, EmbodiedCarbon, EmbodiedEnergy.
// An indicator that the material does not carry is left undefined ("not evaluated").
inline std::vector<Record> TakeoffRecords(const std::vector<LayeredElement>& els, const std::vector<MaterialDef>& mats) {
  std::vector<Record> out;
  for (auto& e : els) {
    for (auto& l : e.layers) {
      const MaterialDef* m = nullptr;
      for (auto& x : mats) if (x.name == l.material) m = &x;
      double area = std::max(0.0, e.faceArea - e.openingArea);
      double vol = area * l.thickness;
      Record r;
      r["Element"] = Value::Of(e.id);
      r["Category"] = Value::Of(e.category);
      r["Material"] = Value::Of(l.material);
      r["Area"] = Value::Of(area);
      r["Thickness"] = Value::Of(l.thickness);
      r["Volume"] = Value::Of(vol);
      if (m) {
        r["Cost"] = Value::Of(vol * m->costPerM3);
        r["Mass"] = Value::Of(vol * m->density);
        if (m->hasCarbon) r["EmbodiedCarbon"] = Value::Of(vol * m->density * m->carbonPerKg);
        if (m->hasEnergy) r["EmbodiedEnergy"] = Value::Of(vol * m->density * m->energyPerKg);
      }
      out.push_back(r);
    }
  }
  return out;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Mark numbering
// ------------------------------------------------------------------------------------------------------------------------------
// 1 -> A, 26 -> Z, 27 -> AA (spreadsheet letters).
inline std::string LettersOf(int n) {
  std::string s;
  while (n > 0) { --n; s.insert(s.begin(), char('A' + n % 26)); n /= 26; }
  return s;
}

struct MarkItem { std::string id, partition; };
struct MarkTemplate {
  bool partitioned = false;       // false: sequential marks in placement order
  std::string prefix;             // e.g. "D"
  std::string partitionSeparator; // between the partition value and the sequence
  bool letters = false;           // sequence as letters instead of numbers
  int width = 0;                  // zero padding of numeric sequences
  int start = 1;
};

// Marks in placement order. Partitioned: the sequence restarts in every partition value. Marks already in `taken` are skipped so the
// result never repeats one. Returns id -> mark.
inline std::map<std::string, std::string> NumberMarks(const std::vector<MarkItem>& items, const MarkTemplate& t, const std::set<std::string>& taken = {}) {
  std::map<std::string, std::string> out;
  std::map<std::string, int> next;
  std::set<std::string> used = taken;
  for (auto& it : items) {
    std::string part = t.partitioned ? it.partition : std::string();
    int& n = next.count(part) ? next[part] : (next[part] = t.start);
    for (;;) {
      std::string seq;
      if (t.letters) seq = LettersOf(n);
      else { seq = std::to_string(n); if ((int)seq.size() < t.width) seq.insert(0, t.width - seq.size(), '0'); }
      std::string mark = t.prefix + (t.partitioned ? part + t.partitionSeparator : std::string()) + seq;
      ++n;
      if (used.count(mark)) continue;
      used.insert(mark);
      out[it.id] = mark;
      break;
    }
  }
  return out;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Revisions
// ------------------------------------------------------------------------------------------------------------------------------
enum class RevNumbering { Numeric, Alphanumeric, None };
enum class RevShow { None, Tag, CloudAndTag };

struct Revision {
  int sequence = 0;               // 1-based position in the project list
  RevNumbering numbering = RevNumbering::Numeric;
  std::string date, description, issuedTo, issuedBy;
  bool issued = false;
  RevShow show = RevShow::CloudAndTag;
  std::string number;             // computed label
};
struct RevCloud { std::string id, sheet; int revisionSequence = 0; };

class RevisionList {
 public:
  int Add(RevNumbering n, const std::string& date, const std::string& desc) {
    Revision r; r.sequence = (int)revs_.size() + 1; r.numbering = n; r.date = date; r.description = desc;
    revs_.push_back(r);
    Renumber();
    return r.sequence;
  }
  const std::vector<Revision>& All() const { return revs_; }
  // Issuing locks a revision: its description, date and numbering cannot be edited and clouds cannot be added to it.
  bool Issue(int seq, const std::string& to, const std::string& by) {
    Revision* r = At(seq);
    if (!r || r->issued) return false;
    r->issued = true; r->issuedTo = to; r->issuedBy = by;
    return true;
  }
  bool SetShow(int seq, RevShow s) { Revision* r = At(seq); if (!r) return false; r->show = s; return true; }
  bool Unissue(int seq) { Revision* r = At(seq); if (!r || !r->issued) return false; r->issued = false; return true; }
  bool EditDescription(int seq, const std::string& d) { Revision* r = At(seq); if (!r || r->issued) return false; r->description = d; return true; }
  bool SetNumbering(int seq, RevNumbering n) { Revision* r = At(seq); if (!r || r->issued) return false; r->numbering = n; Renumber(); return true; }
  bool AddCloud(const std::string& id, const std::string& sheet, int seq) {
    Revision* r = At(seq);
    if (!r || r->issued) return false;
    clouds_.push_back({id, sheet, seq});
    return true;
  }
  const std::vector<RevCloud>& Clouds() const { return clouds_; }

  // Rows of the revision schedule of a sheet: one per revision that has a visible cloud on that sheet, in sequence order, with the
  // ids of those clouds. A revision whose show setting is None is left out.
  struct ScheduleRow { std::string number, date, description; std::vector<std::string> clouds; };
  std::vector<ScheduleRow> ScheduleOnSheet(const std::string& sheet) const {
    std::vector<ScheduleRow> out;
    for (auto& r : revs_) {
      if (r.show == RevShow::None) continue;
      ScheduleRow row;
      for (auto& c : clouds_) if (c.sheet == sheet && c.revisionSequence == r.sequence) row.clouds.push_back(c.id);
      if (row.clouds.empty()) continue;
      row.number = r.number; row.date = r.date; row.description = r.description;
      out.push_back(row);
    }
    return out;
  }

 private:
  Revision* At(int seq) { for (auto& r : revs_) if (r.sequence == seq) return &r; return nullptr; }
  // Each numbering kind counts on its own, in sequence order: numeric 1 2 3, alphanumeric A B C, none blank.
  void Renumber() {
    int nn = 0, na = 0;
    for (auto& r : revs_) {
      if (r.numbering == RevNumbering::Numeric) r.number = std::to_string(++nn);
      else if (r.numbering == RevNumbering::Alphanumeric) r.number = LettersOf(++na);
      else r.number.clear();
    }
  }
  std::vector<Revision> revs_;
  std::vector<RevCloud> clouds_;
};

// ------------------------------------------------------------------------------------------------------------------------------
// Shared model: reservation / borrowing, requests, send, receive, relinquish
// ------------------------------------------------------------------------------------------------------------------------------
struct TeamResult {
  bool ok = false;
  std::string code;      // "ok", "reserved", "outdated", "need-receive", "not-reserved", "unsent-changes", "unknown", "not-owner"
  std::string owner;     // owner named by a refusal
  std::string message;
};
struct TeamRequest { int id = 0; std::string element, requester, owner; };

class TeamProject {
 public:
  explicit TeamProject(bool autoReserve = true) : autoReserve_(autoReserve) {}
  void SetAutoReserve(bool v) { autoReserve_ = v; }

  void AddElement(const std::string& id, const std::string& group = std::string()) {
    Elem e; e.group = group; e.rev = 1; elems_[id] = e;
  }
  // The user's local copy starts at the current server state.
  // Returns false for a user an administrator removed from the project's user list.
  bool Join(const std::string& user) {
    if (removed_.count(user)) return false;
    blocked_.erase(user);
    for (auto& kv : elems_) users_[user].seen[kv.first] = kv.second.rev;
    return true;
  }

  std::string OwnerOf(const std::string& id) const { auto it = elems_.find(id); return it == elems_.end() ? std::string() : it->second.owner; }
  int ServerRev(const std::string& id) const { auto it = elems_.find(id); return it == elems_.end() ? 0 : it->second.rev; }
  int LocalRev(const std::string& user, const std::string& id) const {
    auto u = users_.find(user); if (u == users_.end()) return 0;
    auto s = u->second.seen.find(id); return s == u->second.seen.end() ? 0 : s->second;
  }
  std::vector<std::string> OwnedBy(const std::string& user) const {
    std::vector<std::string> v;
    for (auto& kv : elems_) if (kv.second.owner == user) v.push_back(kv.first);
    return v;
  }
  bool HasUnsent(const std::string& user) const { auto u = users_.find(user); return u != users_.end() && !u->second.dirty.empty(); }

  TeamResult Reserve(const std::string& user, const std::string& id) {
    auto it = elems_.find(id);
    if (it == elems_.end()) return Fail("unknown", "", "unknown element " + id);
    Elem& e = it->second;
    if (!e.owner.empty() && e.owner != user) return Fail("reserved", e.owner, id + " is reserved by " + e.owner);
    if (LocalRev(user, id) < e.rev) return Fail("need-receive", "", id + " changed on the server: receive first");
    e.owner = user;
    return Ok();
  }
  // Editing: the element must be reserved by the user (or reserved automatically), and up to date.
  TeamResult Edit(const std::string& user, const std::string& id) {
    auto it = elems_.find(id);
    if (it == elems_.end()) return Fail("unknown", "", "unknown element " + id);
    Elem& e = it->second;
    if (e.deleted) return Fail("deleted", "", id + " was deleted in the central model");
    if (!e.owner.empty() && e.owner != user) return Fail("reserved", e.owner, id + " is owned by " + e.owner);
    if (LocalRev(user, id) < e.rev) return Fail("outdated", e.owner, id + " is out of date: load the latest changes first");
    if (e.owner.empty()) {
      if (!autoReserve_) return Fail("not-reserved", "", id + " is not reserved: reserve it first");
      e.owner = user;
    }
    users_[user].dirty.insert(id);
    return Ok();
  }
  // Releasing needs the changes to be sent first. Pending requests are granted to the earliest requester.
  TeamResult Release(const std::string& user, const std::string& id) {
    auto it = elems_.find(id);
    if (it == elems_.end()) return Fail("unknown", "", "unknown element " + id);
    if (it->second.owner != user) return Fail("not-owner", it->second.owner, id + " is not owned by " + user);
    if (users_[user].dirty.count(id)) return Fail("unsent-changes", "", "send the changes to " + id + " before releasing it");
    it->second.owner.clear();
    for (auto r = requests_.begin(); r != requests_.end(); ++r)
      if (r->element == id) { it->second.owner = r->requester; requests_.erase(r); break; }
    return Ok();
  }
  // A request is a message to the owner. Returns the request id (0 when the element is free or the user owns it).
  int Request(const std::string& user, const std::string& id) {
    auto it = elems_.find(id);
    if (it == elems_.end() || it->second.owner.empty() || it->second.owner == user) return 0;
    TeamRequest r; r.id = ++nextRequest_; r.element = id; r.requester = user; r.owner = it->second.owner;
    requests_.push_back(r);
    return r.id;
  }
  const std::vector<TeamRequest>& Requests() const { return requests_; }
  // Granting moves the reservation to the requester at once.
  TeamResult Grant(const std::string& owner, int requestId) {
    for (auto r = requests_.begin(); r != requests_.end(); ++r) {
      if (r->id != requestId) continue;
      if (r->owner != owner || elems_[r->element].owner != owner) return Fail("not-owner", elems_[r->element].owner, "only the owner can grant");
      // the owner's unsent edits to the element are sent first (the "Send and Release" step)
      auto& u = users_[owner];
      if (u.dirty.erase(r->element)) { Elem& e = elems_[r->element]; ++e.rev; u.seen[r->element] = e.rev; }
      elems_[r->element].owner = r->requester;
      requests_.erase(r);
      return Ok();
    }
    return Fail("unknown", "", "unknown request");
  }
  TeamResult Decline(const std::string& owner, int requestId) {
    for (auto r = requests_.begin(); r != requests_.end(); ++r)
      if (r->id == requestId) { if (r->owner != owner) return Fail("not-owner", r->owner, "only the owner can decline"); requests_.erase(r); return Ok(); }
    return Fail("unknown", "", "unknown request");
  }

  // Send publishes the unsent edits: every sent element gets the next server revision.
  // Returns -1 when the user was forced out of the project and has not joined again.
  int Send(const std::string& user) {
    if (blocked_.count(user)) return -1;
    auto& u = users_[user];
    int n = 0;
    for (auto& id : u.dirty) { Elem& e = elems_[id]; ++e.rev; u.seen[id] = e.rev; if (u.deletes.count(id)) e.deleted = true; ++n; }
    u.dirty.clear();
    u.deletes.clear();
    return n;
  }
  // Deleting needs the same ownership and freshness as editing; the deletion reaches the server with the next send.
  TeamResult Delete(const std::string& user, const std::string& id) {
    TeamResult r = Edit(user, id);
    if (r.ok) users_[user].deletes.insert(id);
    return r;
  }
  // Elements other users updated or deleted on the server since the user's last receive ("Model Updates").
  struct Updates { std::vector<std::string> updated, deleted; };
  Updates ModelUpdates(const std::string& user) const {
    Updates out;
    for (auto& kv : elems_) {
      if (LocalRev(user, kv.first) >= kv.second.rev) continue;
      (kv.second.deleted ? out.deleted : out.updated).push_back(kv.first);
    }
    return out;
  }
  bool IsDeleted(const std::string& id) const { auto it = elems_.find(id); return it != elems_.end() && it->second.deleted; }
  // An administrator forces a user out: reservations become free, unsent edits are discarded, later sends are refused.
  TeamResult ForceLeave(bool adminAllowed, const std::string& user, bool removeFromUserList = false) {
    if (!adminAllowed) return Fail("not-allowed", "", "only a permitted administrator can force a user to leave");
    for (auto& id : OwnedBy(user)) elems_[id].owner.clear();
    users_[user].dirty.clear();
    users_[user].deletes.clear();
    for (auto r = requests_.begin(); r != requests_.end();) { if (r->requester == user) r = requests_.erase(r); else ++r; }
    blocked_.insert(user);
    if (removeFromUserList) removed_.insert(user);
    return Ok();
  }
  // Receive brings every element changed on the server since the last receive into the local copy; unsent edits stay.
  std::vector<std::string> Receive(const std::string& user) {
    std::vector<std::string> changed;
    auto& u = users_[user];
    for (auto& kv : elems_) {
      int& seen = u.seen[kv.first];
      if (seen < kv.second.rev) { seen = kv.second.rev; changed.push_back(kv.first); }
    }
    return changed;
  }
  // Send, then receive, then (Revit) release every borrowed element.
  void SyncWithCentral(const std::string& user, bool relinquishAll = true) {
    Send(user);
    Receive(user);
    if (relinquishAll) for (auto& id : OwnedBy(user)) elems_[id].owner.clear();
  }
  // Hand back everything without synchronizing: refused while unsent local changes exist.
  TeamResult Relinquish(const std::string& user) {
    if (HasUnsent(user)) return Fail("unsent-changes", "", "synchronize first: local changes exist, ownership is kept");
    for (auto& id : OwnedBy(user)) elems_[id].owner.clear();
    return Ok();
  }

  // Reserve many elements; per group (project region / workset) the counts and the blocked element ids.
  struct GroupOutcome { int reserved = 0; int blocked = 0; std::vector<std::string> blockedIds; };
  std::map<std::string, GroupOutcome> ReserveMany(const std::string& user, const std::vector<std::string>& ids) {
    std::map<std::string, GroupOutcome> out;
    for (auto& id : ids) {
      auto it = elems_.find(id);
      if (it == elems_.end()) continue;
      GroupOutcome& g = out[it->second.group];
      if (Reserve(user, id).ok) ++g.reserved; else { ++g.blocked; g.blockedIds.push_back(id); }
    }
    return out;
  }
  // One request message per distinct owner of the blocked elements.
  std::map<std::string, std::vector<std::string>> RequestBlocked(const std::string& user, const std::vector<std::string>& blockedIds) {
    std::map<std::string, std::vector<std::string>> msgs;
    for (auto& id : blockedIds) if (Request(user, id)) msgs[OwnerOf(id)].push_back(id);
    return msgs;
  }

 private:
  struct Elem { std::string group, owner; int rev = 1; bool deleted = false; };
  struct User { std::map<std::string, int> seen; std::set<std::string> dirty, deletes; };
  static TeamResult Ok() { TeamResult r; r.ok = true; r.code = "ok"; return r; }
  static TeamResult Fail(const std::string& code, const std::string& owner, const std::string& msg) { TeamResult r; r.code = code; r.owner = owner; r.message = msg; return r; }
  bool autoReserve_;
  int nextRequest_ = 0;
  std::map<std::string, Elem> elems_;
  std::map<std::string, User> users_;
  std::vector<TeamRequest> requests_;
  std::set<std::string> blocked_, removed_;
};

// ------------------------------------------------------------------------------------------------------------------------------
// Model comparison
// ------------------------------------------------------------------------------------------------------------------------------
struct ModelElement { std::string id; std::map<std::string, std::string> props; };
struct ModelDiff {
  std::vector<std::string> added, modified, removed;                       // sorted by id
  std::map<std::string, std::vector<std::string>> changedProps;            // modified id -> property names (sorted)
};

inline ModelDiff DiffModels(const std::vector<ModelElement>& older, const std::vector<ModelElement>& newer) {
  ModelDiff d;
  std::map<std::string, const ModelElement*> a, b;
  for (auto& e : older) a[e.id] = &e;
  for (auto& e : newer) b[e.id] = &e;
  for (auto& kv : b) {
    auto it = a.find(kv.first);
    if (it == a.end()) { d.added.push_back(kv.first); continue; }
    std::set<std::string> names;
    for (auto& p : it->second->props) names.insert(p.first);
    for (auto& p : kv.second->props) names.insert(p.first);
    std::vector<std::string> ch;
    for (auto& n : names) {
      auto x = it->second->props.find(n), y = kv.second->props.find(n);
      bool hx = x != it->second->props.end(), hy = y != kv.second->props.end();
      if (hx != hy || (hx && x->second != y->second)) ch.push_back(n);
    }
    if (!ch.empty()) { d.modified.push_back(kv.first); d.changedProps[kv.first] = ch; }
  }
  for (auto& kv : a) if (!b.count(kv.first)) d.removed.push_back(kv.first);
  return d;
}

// Model Compare after a receive looks at 3D elements only: elements whose "Dim" property is "2D" are ignored.
inline ModelDiff DiffModels3D(const std::vector<ModelElement>& older, const std::vector<ModelElement>& newer) {
  auto keep = [](const std::vector<ModelElement>& v) {
    std::vector<ModelElement> o;
    for (auto& e : v) { auto it = e.props.find("Dim"); if (it == e.props.end() || it->second != "2D") o.push_back(e); }
    return o;
  };
  return DiffModels(keep(older), keep(newer));
}

// ------------------------------------------------------------------------------------------------------------------------------
// Phases: status of an element in a view phase, phase filters, merging a phase
// ------------------------------------------------------------------------------------------------------------------------------
enum class PhaseStatus { New, Existing, Demolished, Temporary };

// Status of an element created in `created` and demolished in `demolished` ("" = never) when seen in `viewPhase`. Returns false when the
// element does not exist in that phase (created later, or demolished in an earlier phase) or a phase name is unknown.
inline bool StatusInPhase(const std::vector<std::string>& order, const std::string& created, const std::string& demolished, const std::string& viewPhase, PhaseStatus& out) {
  auto idx = [&](const std::string& n) { for (size_t i = 0; i < order.size(); ++i) if (order[i] == n) return (int)i; return -1; };
  int c = idx(created), v = idx(viewPhase), d = demolished.empty() ? -2 : idx(demolished);
  if (c < 0 || v < 0 || d == -1) return false;
  if (c > v) return false;
  if (d >= 0 && d < v) return false;
  if (c == v) { out = d == v ? PhaseStatus::Temporary : PhaseStatus::New; return true; }
  out = d == v ? PhaseStatus::Demolished : PhaseStatus::Existing;
  return true;
}

// Presets named like the phase filters of the product (the preset contents are from recollection, see the row notes).
inline std::set<PhaseStatus> PhasePreset(const std::string& name) {
  using S = PhaseStatus;
  if (name == "Show All") return {S::New, S::Existing, S::Demolished, S::Temporary};
  if (name == "Show Complete") return {S::New, S::Existing, S::Temporary};
  if (name == "Show New") return {S::New};
  if (name == "Show Demo") return {S::Demolished};
  if (name == "Show Previous + New") return {S::Existing, S::New};
  if (name == "Show Previous + Demo") return {S::Existing, S::Demolished};
  return {};
}

// Keeps the records ("PhaseCreated", "PhaseDemolished") whose status in the schedule's phase is allowed; adds "PhaseStatus".
inline std::vector<Record> FilterByPhase(const std::vector<Record>& recs, const std::vector<std::string>& order, const std::string& viewPhase, const std::set<PhaseStatus>& allowed) {
  static const char* names[] = {"New", "Existing", "Demolished", "Temporary"};
  std::vector<Record> out;
  for (auto& r : recs) {
    Value c = Get(r, "PhaseCreated"), d = Get(r, "PhaseDemolished");
    PhaseStatus st;
    if (!StatusInPhase(order, c.s, d.s, viewPhase, st) || !allowed.count(st)) continue;
    Record x = r;
    x["PhaseStatus"] = Value::Of(names[(int)st]);
    out.push_back(x);
  }
  return out;
}

struct PhaseElement { std::string id, created, demolished; };
struct PhaseModel { std::vector<std::string> order; std::vector<PhaseElement> elements; };

// Deletes `phase` and re-points every element's created and demolished phase to the next (or previous) phase.
// Refused when there is no such neighbour or the phase is unknown.
inline bool MergePhase(PhaseModel& m, const std::string& phase, bool intoNext) {
  size_t i = 0;
  while (i < m.order.size() && m.order[i] != phase) ++i;
  if (i == m.order.size()) return false;
  if (intoNext ? i + 1 >= m.order.size() : i == 0) return false;
  std::string target = m.order[intoNext ? i + 1 : i - 1];
  for (auto& e : m.elements) {
    if (e.created == phase) e.created = target;
    if (e.demolished == phase) e.demolished = target;
  }
  m.order.erase(m.order.begin() + i);
  return true;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Multi-category schedules, sheet lists, embedded schedules
// ------------------------------------------------------------------------------------------------------------------------------
struct CategoryData { std::string name; std::set<std::string> fields; std::vector<Record> items; };

// Records of every category that has ALL the requested fields (a shared parameter missing from a category drops that category out),
// each with a "Category" value. The names of the dropped categories go to `dropped`.
inline std::vector<Record> MultiCategoryRecords(const std::vector<CategoryData>& cats, const std::vector<std::string>& requested, std::vector<std::string>* dropped = nullptr) {
  std::vector<Record> out;
  for (auto& c : cats) {
    bool all = true;
    for (auto& f : requested) if (!c.fields.count(f)) all = false;
    if (!all) { if (dropped) dropped->push_back(c.name); continue; }
    for (auto r : c.items) { r["Category"] = Value::Of(c.name); out.push_back(r); }
  }
  return out;
}

struct SheetInfo { std::string number, name; bool placeholder = false; std::map<std::string, std::string> user; };
// One record per sheet: Number, Name, Placeholder ("Yes" / "No") and the user-defined fields.
inline std::vector<Record> SheetRecords(const std::vector<SheetInfo>& sheets, bool includePlaceholders) {
  std::vector<Record> out;
  for (auto& s : sheets) {
    if (s.placeholder && !includePlaceholders) continue;
    Record r;
    r["Number"] = Value::Of(s.number);
    r["Name"] = Value::Of(s.name);
    r["Placeholder"] = Value::Of(s.placeholder ? "Yes" : "No");
    for (auto& kv : s.user) r[kv.first] = Value::Of(kv.second);
    out.push_back(r);
  }
  return out;
}

inline bool EmbeddedScheduleAllowed(const std::string& category) {
  return category == "Rooms" || category == "Spaces" || category == "Duct Systems" || category == "Piping Systems" || category == "Electrical Circuits";
}

struct EmbeddedResult { bool ok = true; std::string error; std::vector<Row> rows; };

// Parent schedule rows, each followed by the member rows whose `memberKey` equals the parent's `parentKey`. Members without a parent row
// are not listed. Member rows have level 1.
inline EmbeddedResult BuildEmbedded(const std::string& parentCategory, const ScheduleDef& parentDef, const std::vector<Record>& parents, const std::string& parentKey,
                                    const ScheduleDef& memberDef, const std::vector<Record>& members, const std::string& memberKey) {
  EmbeddedResult out;
  if (!EmbeddedScheduleAllowed(parentCategory)) { out.ok = false; out.error = "embedded schedules exist only for rooms, spaces, duct systems, piping systems and circuits"; return out; }
  ScheduleResult p = BuildSchedule(parentDef, parents);
  for (auto& row : p.rows) {
    out.rows.push_back(row);
    if (row.kind != Row::Data || row.recordIdx.empty()) continue;
    std::string key = detail::KeyText(Get(p.recs[row.recordIdx[0]], parentKey));
    std::vector<Record> mine;
    for (auto& m : members) if (detail::KeyText(Get(m, memberKey)) == key) mine.push_back(m);
    if (mine.empty()) continue;
    ScheduleResult m = BuildSchedule(memberDef, mine);
    for (auto& mr : m.rows) { mr.level = 1; out.rows.push_back(mr); }
  }
  return out;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Keynotes: tab-delimited file, hierarchy, legend, XML database with merge / replace and conflict handling
// ------------------------------------------------------------------------------------------------------------------------------
struct Keynote { std::string key, text, parent, ref; };

struct KeynoteTable {
  bool ok = true;
  std::string error;
  std::vector<Keynote> notes;
  const Keynote* Find(const std::string& key) const { for (auto& n : notes) if (n.key == key) return &n; return nullptr; }
  // key of the top-level ancestor (the keynote's division); a top-level key is its own division
  std::string Division(const std::string& key) const {
    std::string k = key;
    for (size_t guard = 0; guard <= notes.size(); ++guard) {
      const Keynote* n = Find(k);
      if (!n || n->parent.empty()) return k;
      k = n->parent;
    }
    return k;
  }
  std::vector<std::string> Children(const std::string& key) const { std::vector<std::string> v; for (auto& n : notes) if (n.parent == key) v.push_back(n.key); return v; }
};

// Lines "key TAB description [TAB parent key]". Blank lines are skipped. A line without a description column, an empty key, a duplicate key
// or a parent that is not in the file is an error that names the line.
inline KeynoteTable ParseKeynoteFile(const std::string& text) {
  KeynoteTable t;
  size_t pos = 0;
  int line = 0;
  while (pos <= text.size()) {
    size_t e = text.find('\n', pos);
    if (e == std::string::npos) e = text.size();
    std::string l = text.substr(pos, e - pos);
    pos = e + 1;
    ++line;
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (l.empty()) continue;
    std::vector<std::string> cols;
    size_t a = 0;
    for (;;) { size_t b = l.find('\t', a); if (b == std::string::npos) { cols.push_back(l.substr(a)); break; } cols.push_back(l.substr(a, b - a)); a = b + 1; }
    if (cols.size() < 2 || cols[0].empty()) { t.ok = false; t.error = "line " + std::to_string(line) + ": need a key and a description"; return t; }
    if (t.Find(cols[0])) { t.ok = false; t.error = "line " + std::to_string(line) + ": duplicate key " + cols[0]; return t; }
    Keynote n; n.key = cols[0]; n.text = cols[1]; if (cols.size() > 2) n.parent = cols[2];
    t.notes.push_back(n);
  }
  for (auto& n : t.notes) if (!n.parent.empty() && !t.Find(n.parent)) { t.ok = false; t.error = "unknown parent " + n.parent + " of " + n.key; return t; }
  return t;
}

struct LegendOptions { bool limitToSheet = false; std::set<std::string> keysOnSheet; std::string division; };
// The legend lists keys with their text in file order: limited to the keys placed in views of the sheet and/or to one division.
inline std::vector<Keynote> KeynoteLegend(const KeynoteTable& t, const LegendOptions& o) {
  std::vector<Keynote> out;
  for (auto& n : t.notes) {
    if (o.limitToSheet && !o.keysOnSheet.count(n.key)) continue;
    if (!o.division.empty() && t.Division(n.key) != o.division) continue;
    out.push_back(n);
  }
  return out;
}

namespace detail {
inline std::string XmlEsc(const std::string& s) {
  std::string o;
  for (char c : s) { if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c; }
  return o;
}
inline std::string XmlUnesc(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '&') {
      if (s.compare(i, 5, "&amp;") == 0) { o += '&'; i += 5; continue; }
      if (s.compare(i, 4, "&lt;") == 0) { o += '<'; i += 4; continue; }
      if (s.compare(i, 4, "&gt;") == 0) { o += '>'; i += 4; continue; }
      if (s.compare(i, 6, "&quot;") == 0) { o += '"'; i += 6; continue; }
    }
    o += s[i++];
  }
  return o;
}
}  // namespace detail

// Exports the whole database, or the keynotes below (and including) folder `folder`.
inline std::string ExportKeynotesXml(const std::vector<Keynote>& db, const std::string& folder = std::string()) {
  KeynoteTable t; t.notes = db;
  std::string out = "<Keynotes>\n";
  for (auto& n : db) {
    if (!folder.empty()) {
      bool in = false;
      std::string k = n.key;
      for (size_t g = 0; g <= db.size() && !in; ++g) { if (k == folder) in = true; const Keynote* p = t.Find(k); if (!p || p->parent.empty()) break; k = p->parent; }
      if (!in) continue;
    }
    out += "<Keynote key=\"" + detail::XmlEsc(n.key) + "\" parent=\"" + detail::XmlEsc(n.parent) + "\" text=\"" + detail::XmlEsc(n.text) + "\" ref=\"" + detail::XmlEsc(n.ref) + "\"/>\n";
  }
  return out + "</Keynotes>\n";
}

inline bool ParseKeynotesXml(const std::string& xml, std::vector<Keynote>& out) {
  out.clear();
  if (xml.find("<Keynotes>") == std::string::npos || xml.find("</Keynotes>") == std::string::npos) return false;
  size_t p = 0;
  for (;;) {
    p = xml.find("<Keynote ", p);
    if (p == std::string::npos) return true;
    size_t end = xml.find("/>", p);
    if (end == std::string::npos) return false;
    std::string tag = xml.substr(p + 9, end - p - 9);
    Keynote n;
    size_t q = 0;
    while (q < tag.size()) {
      while (q < tag.size() && tag[q] == ' ') ++q;
      if (q >= tag.size()) break;
      size_t eq = tag.find("=\"", q);
      if (eq == std::string::npos) return false;
      std::string name = tag.substr(q, eq - q);
      size_t close = tag.find('"', eq + 2);
      if (close == std::string::npos) return false;
      std::string val = detail::XmlUnesc(tag.substr(eq + 2, close - eq - 2));
      if (name == "key") n.key = val; else if (name == "parent") n.parent = val; else if (name == "text") n.text = val; else if (name == "ref") n.ref = val;
      q = close + 1;
    }
    if (n.key.empty()) return false;
    out.push_back(n);
    p = end + 2;
  }
}

enum class ImportMode { Merge, Replace };
enum class ImportConflict { Skip, Overwrite, Append };
struct ImportCounts { int added = 0, skipped = 0, overwritten = 0, appended = 0; };

// Replace discards the database first. Merge adds keys that are new; a key that exists is skipped, overwritten, or the imported entry
// is appended under a new unique key "<key>-1", "<key>-2" ... (the append rule is an assumption).
inline bool ImportKeynotes(std::vector<Keynote>& db, const std::string& xml, ImportMode mode, ImportConflict conflict, ImportCounts* counts = nullptr) {
  std::vector<Keynote> in;
  if (!ParseKeynotesXml(xml, in)) return false;
  ImportCounts c;
  if (mode == ImportMode::Replace) { db = in; c.added = (int)in.size(); if (counts) *counts = c; return true; }
  auto find = [&](const std::string& k) -> Keynote* { for (auto& n : db) if (n.key == k) return &n; return nullptr; };
  for (auto& n : in) {
    Keynote* e = find(n.key);
    if (!e) { db.push_back(n); ++c.added; continue; }
    if (conflict == ImportConflict::Skip) { ++c.skipped; continue; }
    if (conflict == ImportConflict::Overwrite) { *e = n; ++c.overwritten; continue; }
    Keynote a = n;
    for (int i = 1; find(a.key); ++i) a.key = n.key + "-" + std::to_string(i);
    db.push_back(a);
    ++c.appended;
  }
  if (counts) *counts = c;
  return true;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Project browser organisation
// ------------------------------------------------------------------------------------------------------------------------------
struct BrowserItem { std::string name; std::map<std::string, std::string> props; };
struct BrowserNode { std::string label; std::vector<BrowserNode> children; std::vector<std::string> items; };

// Groups by up to six properties (more is refused), group labels ascending, items inside a group by `sortBy` then name. An item without
// a grouping property goes to the group "<none>" (an assumption).
inline bool OrganizeBrowser(const std::vector<BrowserItem>& items, const std::vector<std::string>& groupBy, const std::string& sortBy, BrowserNode& root) {
  if (groupBy.size() > 6) return false;
  root = BrowserNode();
  auto prop = [](const BrowserItem& it, const std::string& p) { auto f = it.props.find(p); return f == it.props.end() || f->second.empty() ? std::string("<none>") : f->second; };
  std::vector<const BrowserItem*> sorted;
  for (auto& it : items) sorted.push_back(&it);
  std::stable_sort(sorted.begin(), sorted.end(), [&](const BrowserItem* a, const BrowserItem* b) {
    if (!sortBy.empty()) { std::string x = prop(*a, sortBy), y = prop(*b, sortBy); if (x != y) return x < y; }
    return a->name < b->name;
  });
  for (auto* it : sorted) {
    BrowserNode* n = &root;
    for (auto& g : groupBy) {
      std::string label = prop(*it, g);
      BrowserNode* found = nullptr;
      for (auto& c : n->children) if (c.label == label) found = &c;
      if (!found) {
        n->children.push_back(BrowserNode());
        n->children.back().label = label;
        std::stable_sort(n->children.begin(), n->children.end(), [](const BrowserNode& a, const BrowserNode& b) { return a.label < b.label; });
        for (auto& c : n->children) if (c.label == label) found = &c;
      }
      n = found;
    }
    n->items.push_back(it->name);
  }
  return true;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Zones
// ------------------------------------------------------------------------------------------------------------------------------
struct ZoneInfo { std::string id, name; double measuredArea = 0, reducedArea = 0; };
struct OpeningInfo { std::string zone, kind; double width = 0, height = 0; };  // kind: "Door" or "Window"

// One record per zone: Zone, Name, MeasuredArea, ReducedArea, and for doors and windows inside it the count, the sum of widths and the
// sum of surface areas (width x height): Doors, DoorWidth, DoorSurface, Windows, WindowWidth, WindowSurface.
inline std::vector<Record> ZoneRecords(const std::vector<ZoneInfo>& zones, const std::vector<OpeningInfo>& openings) {
  std::vector<Record> out;
  for (auto& z : zones) {
    double n[2] = {0, 0}, w[2] = {0, 0}, a[2] = {0, 0};
    for (auto& o : openings) {
      if (o.zone != z.id) continue;
      int k = o.kind == "Window" ? 1 : 0;
      n[k] += 1; w[k] += o.width; a[k] += o.width * o.height;
    }
    Record r;
    r["Zone"] = Value::Of(z.id); r["Name"] = Value::Of(z.name);
    r["MeasuredArea"] = Value::Of(z.measuredArea); r["ReducedArea"] = Value::Of(z.reducedArea);
    r["Doors"] = Value::Of(n[0]); r["DoorWidth"] = Value::Of(w[0]); r["DoorSurface"] = Value::Of(a[0]);
    r["Windows"] = Value::Of(n[1]); r["WindowWidth"] = Value::Of(w[1]); r["WindowSurface"] = Value::Of(a[1]);
    out.push_back(r);
  }
  return out;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Snapshots on a schedule with keep-last-N retention
// ------------------------------------------------------------------------------------------------------------------------------
struct SnapshotPolicy { long long intervalSec = 3600; int keepLast = 3; };
class SnapshotStore {
 public:
  struct Snap { long long time; std::string state; };
  explicit SnapshotStore(SnapshotPolicy p) : pol_(p) {}
  // Takes a snapshot when none exists yet or the interval has passed since the last one; then drops the oldest beyond keepLast.
  bool Tick(long long now, const std::string& state) {
    if (!snaps_.empty() && now - snaps_.back().time < pol_.intervalSec) return false;
    snaps_.push_back({now, state});
    while ((int)snaps_.size() > pol_.keepLast) snaps_.erase(snaps_.begin());
    return true;
  }
  const std::vector<Snap>& All() const { return snaps_; }
  // The saved state of the snapshot taken at `time` (rollback overwrites the project with it, or creates a new project from it).
  bool Rollback(long long time, std::string& state) const {
    for (auto& s : snaps_) if (s.time == time) { state = s.state; return true; }
    return false;
  }
 private:
  SnapshotPolicy pol_;
  std::vector<Snap> snaps_;
};

// ------------------------------------------------------------------------------------------------------------------------------
// Worksets (Revit): ownership of whole worksets, borrowing of elements, open / closed, reassignment, who holds what
// ------------------------------------------------------------------------------------------------------------------------------
enum class WorksetKind { User, Standard, Family, View };
struct WorksetInfo { std::string name; WorksetKind kind = WorksetKind::User; std::string owner; bool open = true; };

class WorksetModel {
 public:
  bool Create(const std::string& name, WorksetKind kind = WorksetKind::User) {
    if (name.empty() || Find(name)) return false;
    WorksetInfo w; w.name = name; w.kind = kind; ws_.push_back(w);
    return true;
  }
  // Family worksets cannot be renamed. The new name must be free.
  bool Rename(const std::string& from, const std::string& to) {
    WorksetInfo* w = Find(from);
    if (!w || w->kind == WorksetKind::Family || to.empty() || Find(to)) return false;
    for (auto& e : els_) if (e.workset == from) e.workset = to;
    w->name = to;
    return true;
  }
  bool SetOpen(const std::string& name, bool open) { WorksetInfo* w = Find(name); if (!w) return false; w->open = open; return true; }
  const WorksetInfo* Get(const std::string& name) const { for (auto& w : ws_) if (w.name == name) return &w; return nullptr; }
  void AddElement(const std::string& id, const std::string& workset) { els_.push_back({id, workset, ""}); }
  std::string WorksetOf(const std::string& id) const { for (auto& e : els_) if (e.id == id) return e.workset; return ""; }

  // Makes the worksets editable for `user` in one action: each free workset becomes owned; one owned by someone else is refused naming
  // the owner. The others are still taken (partial success is an assumption).
  std::map<std::string, TeamResult> MakeEditable(const std::string& user, const std::vector<std::string>& names) {
    std::map<std::string, TeamResult> out;
    for (auto& n : names) {
      WorksetInfo* w = Find(n);
      TeamResult r;
      if (!w) { r.code = "unknown"; r.message = "unknown workset " + n; }
      else if (!w->owner.empty() && w->owner != user) { r.code = "owned"; r.owner = w->owner; r.message = n + " is owned by " + w->owner; }
      else { w->owner = user; r.ok = true; r.code = "ok"; }
      out[n] = r;
    }
    return out;
  }
  // Borrowing an element needs its workset open and not owned by another user, and the element not borrowed by another user.
  TeamResult Edit(const std::string& user, const std::string& id) {
    Elem* e = FindEl(id);
    TeamResult r;
    if (!e) { r.code = "unknown"; return r; }
    const WorksetInfo* w = Get(e->workset);
    if (!w->open) { r.code = "closed"; r.message = e->workset + " is closed"; return r; }
    if (!w->owner.empty() && w->owner != user) { r.code = "owned"; r.owner = w->owner; r.message = id + " is in workset " + e->workset + " owned by " + w->owner; return r; }
    if (!e->borrower.empty() && e->borrower != user) { r.code = "borrowed"; r.owner = e->borrower; r.message = id + " is borrowed by " + e->borrower; return r; }
    if (w->owner != user) e->borrower = user;
    r.ok = true; r.code = "ok";
    return r;
  }
  // Moves the elements to another workset. Each element must be editable by the user, and the target must be open and not owned by
  // another user (the target rule is an assumption). Returns the ids that moved.
  std::vector<std::string> Reassign(const std::string& user, const std::vector<std::string>& ids, const std::string& target) {
    std::vector<std::string> moved;
    const WorksetInfo* t = Get(target);
    if (!t || !t->open || (!t->owner.empty() && t->owner != user)) return moved;
    for (auto& id : ids) {
      Elem* e = FindEl(id);
      if (!e || !Edit(user, id).ok) continue;
      e->workset = target;
      moved.push_back(id);
    }
    return moved;
  }
  std::vector<std::string> VisibleElements() const {
    std::vector<std::string> v;
    for (auto& e : els_) if (Get(e.workset)->open) v.push_back(e.id);
    return v;
  }
  // Hands back owned worksets and borrowed elements (the caller checks unsynchronized changes).
  void ReleaseAll(const std::string& user) {
    for (auto& w : ws_) if (w.owner == user) w.owner.clear();
    for (auto& e : els_) if (e.borrower == user) e.borrower.clear();
  }
  struct Holding { std::string owner; std::set<std::string> borrowers; };
  // Per workset: its owner and the users borrowing elements in it.
  std::map<std::string, Holding> Report() const {
    std::map<std::string, Holding> out;
    for (auto& w : ws_) out[w.name].owner = w.owner;
    for (auto& e : els_) if (!e.borrower.empty()) out[e.workset].borrowers.insert(e.borrower);
    return out;
  }
  // The editor of an element: its borrower, or else the owner of its workset ("" when nobody holds it).
  std::string EditorOf(const std::string& id) const {
    for (auto& e : els_) if (e.id == id) { if (!e.borrower.empty()) return e.borrower; const WorksetInfo* w = Get(e.workset); return w ? w->owner : ""; }
    return "";
  }
 private:
  struct Elem { std::string id, workset, borrower; };
  WorksetInfo* Find(const std::string& n) { for (auto& w : ws_) if (w.name == n) return &w; return nullptr; }
  Elem* FindEl(const std::string& id) { for (auto& e : els_) if (e.id == id) return &e; return nullptr; }
  std::vector<WorksetInfo> ws_;
  std::vector<Elem> els_;
};

// ------------------------------------------------------------------------------------------------------------------------------
// Issues (Archicad issue handling data model)
// ------------------------------------------------------------------------------------------------------------------------------
struct Issue {
  int id = 0;
  std::string title, status, priority, type, creator, responsible, due;  // due: YYYY-MM-DD
  std::vector<std::string> elements;
  std::string view;
};
struct IssueNotice { std::string to, text; int issue = 0; };

inline bool ValidDate(const std::string& d) {
  if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
  for (size_t i = 0; i < d.size(); ++i) if (i != 4 && i != 7 && (d[i] < '0' || d[i] > '9')) return false;
  int y = std::stoi(d.substr(0, 4)), m = std::stoi(d.substr(5, 2)), day = std::stoi(d.substr(8, 2));
  static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12 || day < 1) return false;
  int mx = dim[m - 1] + ((m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ? 1 : 0);
  return day <= mx;
}

class IssueTracker {
 public:
  IssueTracker(std::set<std::string> members, std::vector<std::string> statuses = {"Open", "In Progress", "Resolved", "Closed"},
               std::vector<std::string> priorities = {"Low", "Normal", "High", "Critical"})
      : members_(std::move(members)), statuses_(std::move(statuses)), priorities_(std::move(priorities)) {}
  // Returns the new issue id, or 0 with `error` set. A title, a known status and priority, a valid due date (or none) and a project
  // member as creator are required.
  int Create(const std::string& creator, Issue i, std::string* error = nullptr) {
    auto fail = [&](const char* m) { if (error) *error = m; return 0; };
    if (!members_.count(creator)) return fail("creator is not a project member");
    if (i.title.empty()) return fail("title required");
    if (std::find(statuses_.begin(), statuses_.end(), i.status) == statuses_.end()) return fail("unknown status");
    if (std::find(priorities_.begin(), priorities_.end(), i.priority) == priorities_.end()) return fail("unknown priority");
    if (!i.due.empty() && !ValidDate(i.due)) return fail("invalid due date");
    i.id = ++next_; i.creator = creator; i.responsible.clear();
    issues_.push_back(i);
    return i.id;
  }
  // Assigns to a project member; the member gets a notice and becomes the issue's responsible person.
  bool Assign(int id, const std::string& user) {
    Issue* i = Find(id);
    if (!i || !members_.count(user)) return false;
    i->responsible = user;
    notices_.push_back({user, "issue assigned: " + i->title, id});
    return true;
  }
  bool SetStatus(int id, const std::string& status) {
    Issue* i = Find(id);
    if (!i || std::find(statuses_.begin(), statuses_.end(), status) == statuses_.end()) return false;
    i->status = status;
    return true;
  }
  // Issues are visible to every member (and to nobody else).
  std::vector<Issue> VisibleTo(const std::string& user) const { return members_.count(user) ? issues_ : std::vector<Issue>(); }
  // Issues past their due date (strictly before `today`) and not closed or resolved.
  std::vector<int> Overdue(const std::string& today) const {
    std::vector<int> v;
    for (auto& i : issues_) if (!i.due.empty() && i.due < today && i.status != "Closed" && i.status != "Resolved") v.push_back(i.id);
    return v;
  }
  const std::vector<IssueNotice>& Notices() const { return notices_; }
 private:
  Issue* Find(int id) { for (auto& i : issues_) if (i.id == id) return &i; return nullptr; }
  std::set<std::string> members_;
  std::vector<std::string> statuses_, priorities_;
  std::vector<Issue> issues_;
  std::vector<IssueNotice> notices_;
  int next_ = 0;
};

// ------------------------------------------------------------------------------------------------------------------------------
// Attributes (Archicad): reserved per type, new ones stay local until sent
// ------------------------------------------------------------------------------------------------------------------------------
class AttributeProject {
 public:
  explicit AttributeProject(std::vector<std::string> types) : types_(std::move(types)) {}
  TeamResult Reserve(const std::string& user, const std::string& type) {
    auto it = owner_.find(type);
    if (std::find(types_.begin(), types_.end(), type) == types_.end()) return Fail("unknown", "", "unknown attribute type");
    if (it != owner_.end() && it->second != user) return Fail("reserved", it->second, type + " is reserved by " + it->second);
    owner_[type] = user;
    return Ok();
  }
  // All types at once: returns the types that could not be reserved.
  std::vector<std::string> ReserveAll(const std::string& user) {
    std::vector<std::string> blocked;
    for (auto& t : types_) if (!Reserve(user, t).ok) blocked.push_back(t);
    return blocked;
  }
  void Release(const std::string& user, const std::string& type) { auto it = owner_.find(type); if (it != owner_.end() && it->second == user) owner_.erase(it); }
  // Anyone may create; the attribute stays in the creator's local copy until sent.
  void Create(const std::string& user, const std::string& type, const std::string& name, const std::string& value) { local_[user][type][name] = value; created_[user].insert(type + "/" + name); }
  // Modifying or deleting a server attribute needs the type reserved by the user; an attribute the user created and has not sent is free.
  TeamResult Modify(const std::string& user, const std::string& type, const std::string& name, const std::string& value) {
    if (!created_[user].count(type + "/" + name)) {
      auto it = owner_.find(type);
      if (it == owner_.end()) return Fail("not-reserved", "", type + " is not reserved");
      if (it->second != user) return Fail("reserved", it->second, type + " is reserved by " + it->second);
    }
    if (!local_[user][type].count(name)) return Fail("unknown", "", "no such attribute");
    local_[user][type][name] = value;
    modified_[user].insert(type + "/" + name);
    return Ok();
  }
  void Send(const std::string& user) {
    for (auto& t : types_) for (auto& kv : local_[user][t]) {
      std::string k = t + "/" + kv.first;
      if (created_[user].count(k) || modified_[user].count(k)) server_[t][kv.first] = kv.second;
    }
    created_[user].clear(); modified_[user].clear();
  }
  // Server attributes replace the local ones that the user has not changed; local unsent ones stay.
  void Receive(const std::string& user) {
    for (auto& t : types_) for (auto& kv : server_[t]) {
      std::string k = t + "/" + kv.first;
      if (!created_[user].count(k) && !modified_[user].count(k)) local_[user][t][kv.first] = kv.second;
    }
  }
  bool Has(const std::string& user, const std::string& type, const std::string& name, std::string* value = nullptr) {
    auto& m = local_[user][type];
    auto it = m.find(name);
    if (it == m.end()) return false;
    if (value) *value = it->second;
    return true;
  }
  // Seeds an attribute that already exists on the server (every user receives it).
  void Seed(const std::string& type, const std::string& name, const std::string& value) { server_[type][name] = value; }
 private:
  static TeamResult Ok() { TeamResult r; r.ok = true; r.code = "ok"; return r; }
  static TeamResult Fail(const std::string& c, const std::string& o, const std::string& m) { TeamResult r; r.code = c; r.owner = o; r.message = m; return r; }
  std::vector<std::string> types_;
  std::map<std::string, std::string> owner_;
  std::map<std::string, std::map<std::string, std::string>> server_;
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>> local_;
  std::map<std::string, std::set<std::string>> created_, modified_;
};

}  // namespace bimdoc
}  // namespace kernel
}  // namespace dino8
