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
enum class Op { Eq, Ne, Gt, Ge, Lt, Le, Contains, NotContains, BeginsWith, EndsWith, HasValue, HasNoValue, ClassIs, ClassIsNot, ClassUnder };

struct Condition {
  std::string field;
  Op op = Op::Eq;
  Value value;
};

// All comparisons are case-sensitive. A comparison with an undefined field is false (except Ne / NotContains / ClassIsNot,
// which are true when the field is undefined, and HasNoValue).
inline bool Test(const Condition& c, const Record& r) {
  Value v = Get(r, c.field);
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
enum class Aggregate { None, Total, Min, Max };
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
};

struct Row {
  enum Kind { Data, GroupHeader, GroupFooter, Blank, GrandTotal } kind = Data;
  std::vector<std::string> cells;      // visible fields only
  std::string label;                   // header / footer / grand total title
  int count = 0;                       // records represented by the row
  int level = 0;
};

struct ScheduleResult {
  bool ok = true;
  std::string error;
  std::vector<Row> rows;
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
inline std::vector<Record> Prepare(const ScheduleDef& def, std::vector<Record> recs) {
  for (auto& f : def.fields) {
    if (f.kind == FieldKind::Formula) {
      for (auto& r : recs) {
        Value v;
        if (!Formula::Eval(f.formula, r, v)) v = Value();
        r[f.name] = v;
      }
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
  std::stable_sort(recs.begin(), recs.end(), [&](const Record& a, const Record& b) {
    for (auto& k : def.sort) {
      int c = detail::CompareValues(Get(a, k.field), Get(b, k.field));
      if (c) return k.descending ? c > 0 : c < 0;
    }
    return false;
  });
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
      double acc = 0;
      for (size_t i : rs) {
        Value v = Get(recs[i], f.name);
        if (v.kind != Value::Num) continue;
        double x = v.n;
        if (f.aggregate == Aggregate::Total && grand && def.roundedGrandTotals && f.decimals >= 0) x = RoundTo(x * f.scale, f.decimals) / (f.scale == 0 ? 1 : f.scale);
        if (!any) acc = x;
        else if (f.aggregate == Aggregate::Total) acc += x;
        else if (f.aggregate == Aggregate::Min) acc = std::min(acc, x);
        else acc = std::max(acc, x);
        any = true;
      }
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
    Row r; r.kind = Row::Data; r.cells = cellsFor(idx); r.count = (int)idx.size();
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
  void Join(const std::string& user) { for (auto& kv : elems_) users_[user].seen[kv.first] = kv.second.rev; }

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
  int Send(const std::string& user) {
    auto& u = users_[user];
    int n = 0;
    for (auto& id : u.dirty) { Elem& e = elems_[id]; ++e.rev; u.seen[id] = e.rev; ++n; }
    u.dirty.clear();
    return n;
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
  struct Elem { std::string group, owner; int rev = 1; };
  struct User { std::map<std::string, int> seen; std::set<std::string> dirty; };
  static TeamResult Ok() { TeamResult r; r.ok = true; r.code = "ok"; return r; }
  static TeamResult Fail(const std::string& code, const std::string& owner, const std::string& msg) { TeamResult r; r.code = code; r.owner = owner; r.message = msg; return r; }
  bool autoReserve_;
  int nextRequest_ = 0;
  std::map<std::string, Elem> elems_;
  std::map<std::string, User> users_;
  std::vector<TeamRequest> requests_;
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

}  // namespace bimdoc
}  // namespace kernel
}  // namespace dino8
