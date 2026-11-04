#include "script/Lisp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

namespace dino8::lisp {

namespace {

constexpr long long kMaxSteps = 3000000;
constexpr int kMaxDepth = 300;

Value Nil() { return Value{}; }
Value True() { Value v; v.t = Value::T::True; return v; }
Value Int(long long i) { Value v; v.t = Value::T::Int; v.i = i; return v; }
Value Real(double r) { Value v; v.t = Value::T::Real; v.r = r; return v; }
Value Str(std::string s) { Value v; v.t = Value::T::Str; v.s = std::move(s); return v; }
Value Sym(std::string s) { Value v; v.t = Value::T::Sym; v.s = std::move(s); return v; }
Value MakeList(List l) {
  if (l.empty()) return Nil();
  Value v; v.t = Value::T::List; v.l = std::make_shared<List>(std::move(l)); return v;
}
Value Bool(bool b) { return b ? True() : Nil(); }
Value Pair(Value a, Value b) {
  Value v = MakeList({std::move(a), std::move(b)});
  v.dotted = true;
  return v;
}
// A selection set: always a (possibly empty) list object, so ssadd can fill it in place.
Value MakeSet(List l) {
  Value v;
  v.t = Value::T::List;
  v.l = std::make_shared<List>(std::move(l));
  v.pickset = true;
  return v;
}

bool IsList(const Value& v) { return v.t == Value::T::List; }
bool IsNil(const Value& v) { return v.t == Value::T::Nil; }
bool IsNum(const Value& v) { return v.t == Value::T::Int || v.t == Value::T::Real; }
bool Truthy(const Value& v) { return v.t != Value::T::Nil; }
double D(const Value& v) { return v.t == Value::T::Int ? static_cast<double>(v.i) : v.r; }
const List& Items(const Value& v) {
  static const List empty;
  return IsList(v) ? *v.l : empty;
}

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string FormatReal(double r) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.6f", r);
  std::string s = buf;
  while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
  if (s == "-0.0") s = "0.0";
  return s;
}

[[noreturn]] void Fail(const std::string& m) { throw LispError(m); }

void NeedArgs(const char* fn, const List& a, size_t lo, size_t hi) {
  if (a.size() < lo || a.size() > hi) Fail(std::string("too ") + (a.size() < lo ? "few" : "many") + " arguments to " + fn);
}
double NeedNum(const char* fn, const Value& v) {
  if (!IsNum(v)) Fail(std::string("bad argument type: numberp: ") + Interp::Format(v, true) + " in " + fn);
  return D(v);
}
const std::string& NeedStr(const char* fn, const Value& v) {
  if (v.t != Value::T::Str) Fail(std::string("bad argument type: stringp: ") + Interp::Format(v, true) + " in " + fn);
  return v.s;
}
const List& NeedList(const char* fn, const Value& v) {
  if (!IsList(v) && !IsNil(v)) Fail(std::string("bad argument type: listp: ") + Interp::Format(v, true) + " in " + fn);
  return Items(v);
}

bool Equal(const Value& a, const Value& b, double fuzz = 0) {
  if (IsNum(a) && IsNum(b)) return std::fabs(D(a) - D(b)) <= fuzz;
  if (a.t != b.t) return false;
  switch (a.t) {
    case Value::T::Nil: case Value::T::True: return true;
    case Value::T::Str: case Value::T::Sym: case Value::T::Subr: return a.s == b.s;
    case Value::T::List: {
      if (a.dotted != b.dotted || a.l->size() != b.l->size()) return false;
      for (size_t k = 0; k < a.l->size(); ++k) if (!Equal((*a.l)[k], (*b.l)[k], fuzz)) return false;
      return true;
    }
    case Value::T::Lambda: return a.f == b.f;
    default: return false;
  }
}

// ---- parser ---------------------------------------------------------------
struct Parser {
  const std::string& src;
  size_t pos = 0;
  explicit Parser(const std::string& s) : src(s) {}

  void SkipSpace() {
    while (pos < src.size()) {
      char c = src[pos];
      if (std::isspace(static_cast<unsigned char>(c))) { ++pos; continue; }
      if (c == ';') {
        if (pos + 1 < src.size() && src[pos + 1] == '|') {
          size_t e = src.find("|;", pos + 2);
          if (e == std::string::npos) Fail("malformed comment: missing |;");
          pos = e + 2;
        } else {
          while (pos < src.size() && src[pos] != '\n') ++pos;
        }
        continue;
      }
      break;
    }
  }
  bool AtEnd() { SkipSpace(); return pos >= src.size(); }

  Value ParseOne() {
    SkipSpace();
    if (pos >= src.size()) Fail("unexpected end of input");
    char c = src[pos];
    if (c == '(') {
      ++pos;
      List items;
      for (;;) {
        SkipSpace();
        if (pos >= src.size()) Fail("malformed list: missing close parenthesis");
        if (src[pos] == ')') { ++pos; break; }
        Value item = ParseOne();
        if (item.t == Value::T::Sym && item.s == "." && items.size() == 1) {
          // (a . b): a dotted pair; (a . (b c)) is the list (a b c) and (a . nil) is (a)
          Value tail = ParseOne();
          SkipSpace();
          if (pos >= src.size() || src[pos] != ')') Fail("malformed dotted pair: one value must follow the dot");
          ++pos;
          if (IsList(tail) && !tail.dotted) { List l{items[0]}; for (const Value& v : *tail.l) l.push_back(v); return MakeList(std::move(l)); }
          if (IsNil(tail)) return MakeList({items[0]});
          return Pair(items[0], tail);
        }
        items.push_back(std::move(item));
      }
      return MakeList(std::move(items));
    }
    if (c == ')') Fail("extra close parenthesis");
    if (c == '\'') {
      ++pos;
      return MakeList({Sym("quote"), ParseOne()});
    }
    if (c == '"') {
      ++pos;
      std::string out;
      for (;;) {
        if (pos >= src.size()) Fail("malformed string: missing closing quote");
        char d = src[pos++];
        if (d == '"') break;
        if (d == '\\' && pos < src.size()) {
          char e = src[pos++];
          switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'e': out += '\x1b'; break;
            case 'r': out += '\r'; break;
            default: out += e; break;
          }
        } else {
          out += d;
        }
      }
      return Str(out);
    }
    size_t s = pos;
    while (pos < src.size() && !std::isspace(static_cast<unsigned char>(src[pos])) && src[pos] != '(' && src[pos] != ')' &&
           src[pos] != '"' && src[pos] != ';' && src[pos] != '\'')
      ++pos;
    const std::string tok = src.substr(s, pos - s);
    if (tok.empty()) Fail("unexpected character");
    char* end = nullptr;
    const long long iv = std::strtoll(tok.c_str(), &end, 10);
    if (end && *end == 0) return Int(iv);
    const double dv = std::strtod(tok.c_str(), &end);
    if (end && *end == 0 && (std::isdigit(static_cast<unsigned char>(tok.back())) || tok.back() == '.')) return Real(dv);
    const std::string low = Lower(tok);
    if (low == "nil") return Nil();
    if (low == "t") return True();
    return Sym(low);
  }
};

// ---- entity / input helpers ---------------------------------------------------
long long NeedEname(const char* fn, const Value& v) {
  if (v.t != Value::T::Int) Fail(std::string("bad argument type: ename: ") + Interp::Format(v, true) + " in " + fn);
  return v.i;
}

Value DxfToValue(const DxfItem& d) {
  switch (d.k) {
    case DxfItem::K::Str: return Pair(Int(d.code), Str(d.s));
    case DxfItem::K::Real: return Pair(Int(d.code), Real(d.x));
    case DxfItem::K::Int: return Pair(Int(d.code), Int(d.i));
    case DxfItem::K::Point: return MakeList({Int(d.code), Real(d.x), Real(d.y), Real(d.z)});
  }
  return Nil();
}

// An association list (as (entget ...) returns it) back into groups.
std::vector<DxfItem> ValueToDxf(const Value& alist) {
  std::vector<DxfItem> out;
  for (const Value& e : NeedList("entmod", alist)) {
    if (!IsList(e) || e.l->size() < 2 || (*e.l)[0].t != Value::T::Int) Fail("bad association list: every entry must be (code . value) or (code x y z)");
    DxfItem d;
    d.code = static_cast<int>((*e.l)[0].i);
    const Value& v = (*e.l)[1];
    if (e.dotted || e.l->size() == 2) {
      if (v.t == Value::T::Str) { d.k = DxfItem::K::Str; d.s = v.s; }
      else if (v.t == Value::T::Int) { d.k = DxfItem::K::Int; d.i = v.i; d.x = static_cast<double>(v.i); }
      else if (v.t == Value::T::Real) { d.k = DxfItem::K::Real; d.x = v.r; }
      else Fail("bad association list: group " + std::to_string(d.code) + " has an unsupported value");
    } else {
      if (e.l->size() != 3 && e.l->size() != 4) Fail("bad association list: a point group needs 2 or 3 coordinates");
      d.k = DxfItem::K::Point;
      for (size_t k = 1; k < e.l->size(); ++k) {
        if (!IsNum((*e.l)[k])) Fail("bad association list: point coordinates must be numbers");
        (k == 1 ? d.x : k == 2 ? d.y : d.z) = D((*e.l)[k]);
      }
    }
    out.push_back(std::move(d));
  }
  return out;
}

// Case-insensitive wildcard match: * any run, ? one character.
bool WildOne(const std::string& p, size_t pi, const std::string& s, size_t si) {
  while (pi < p.size()) {
    if (p[pi] == '*') {
      while (pi < p.size() && p[pi] == '*') ++pi;
      if (pi == p.size()) return true;
      for (size_t k = si; k <= s.size(); ++k) if (WildOne(p, pi, s, k)) return true;
      return false;
    }
    if (si >= s.size()) return false;
    if (p[pi] != '?' && std::tolower(static_cast<unsigned char>(p[pi])) != std::tolower(static_cast<unsigned char>(s[si]))) return false;
    ++pi; ++si;
  }
  return si == s.size();
}
// "LINE,CIRCLE" alternatives; a leading ~ negates the whole pattern.
bool WildMatch(std::string pattern, const std::string& s) {
  bool neg = false;
  if (!pattern.empty() && pattern[0] == '~') { neg = true; pattern.erase(0, 1); }
  bool hit = false;
  size_t from = 0;
  for (;;) {
    const size_t c = pattern.find(',', from);
    const std::string alt = pattern.substr(from, c == std::string::npos ? std::string::npos : c - from);
    if (WildOne(alt, 0, s, 0)) { hit = true; break; }
    if (c == std::string::npos) break;
    from = c + 1;
  }
  return neg ? !hit : hit;
}

// Does the entity (as an association list) satisfy every pair of the filter list?
bool EntMatches(const List& ent, const List& filter) {
  for (const Value& f : filter) {
    if (!IsList(f) || f.l->empty() || (*f.l)[0].t != Value::T::Int) Fail("bad ssget filter: every entry must be (code . value)");
    const long long code = (*f.l)[0].i;
    if (code == -4) Fail("ssget filter operators (-4 . \"<OR\") are not supported; every pair must hold");
    bool any = false;
    bool saw = false;
    for (const Value& e : ent) {
      if (!IsList(e) || e.l->empty() || (*e.l)[0].t != Value::T::Int || (*e.l)[0].i != code) continue;
      saw = true;
      if (f.dotted || f.l->size() == 2) {
        const Value& want = (*f.l)[1];
        const Value& have = (*e.l)[1];
        if (want.t == Value::T::Str && have.t == Value::T::Str) any = any || WildMatch(want.s, have.s);
        else if (IsNum(want) && IsNum(have)) any = any || std::fabs(D(want) - D(have)) <= 1e-9;
      } else if (f.l->size() == e.l->size()) {
        bool same = true;
        for (size_t k = 1; k < f.l->size(); ++k) same = same && IsNum((*f.l)[k]) && IsNum((*e.l)[k]) && std::fabs(D((*f.l)[k]) - D((*e.l)[k])) <= 1e-9;
        any = any || same;
      }
    }
    // A BYLAYER entity carries no group 62; the filter value 256 (BYLAYER) selects it.
    if (!saw && code == 62 && f.l->size() == 2 && IsNum((*f.l)[1]) && D((*f.l)[1]) == 256) any = true;
    if (!any) return false;
  }
  return true;
}

// ---- typed input (getint, getreal, ...) -------------------------------------
bool IsEnterTok(const std::string& s) { return s.empty() || Lower(s) == "enter" || s == "_Enter"; }
bool IsCancelTok(const std::string& s) { const std::string l = Lower(s); return l == "cancel" || l == "_cancel" || l == "!cancel"; }

// initget keyword definitions: "Yes No"; the leading capitals of a keyword are its shortest abbreviation.
std::optional<std::string> MatchKeyword(const std::string& defs, const std::string& input) {
  if (input.empty()) return std::nullopt;
  size_t i = 0;
  while (i < defs.size()) {
    while (i < defs.size() && (std::isspace(static_cast<unsigned char>(defs[i])) || defs[i] == ',')) ++i;
    size_t j = i;
    while (j < defs.size() && !std::isspace(static_cast<unsigned char>(defs[j])) && defs[j] != ',') ++j;
    const std::string kw = defs.substr(i, j - i);
    i = j;
    if (kw.empty() || kw[0] == '_') continue;
    size_t req = 0;
    while (req < kw.size() && std::isupper(static_cast<unsigned char>(kw[req]))) ++req;
    if (req == 0) req = kw.size();
    if (input.size() >= req && input.size() <= kw.size() && Lower(kw).compare(0, input.size(), Lower(input)) == 0) return kw;
  }
  return std::nullopt;
}

bool ParseWholeInt(const std::string& s, long long& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  out = std::strtoll(s.c_str(), &end, 10);
  return end && *end == 0;
}
bool ParseWholeReal(const std::string& s, double& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  out = std::strtod(s.c_str(), &end);
  return end && *end == 0 && end != s.c_str() && std::isfinite(out);
}
bool ParsePointText(const std::string& s, double p[3]) {
  p[0] = p[1] = p[2] = 0;
  size_t from = 0;
  int n = 0;
  for (;;) {
    const size_t c = s.find(',', from);
    if (n >= 3) return false;
    if (!ParseWholeReal(s.substr(from, c == std::string::npos ? std::string::npos : c - from), p[n])) return false;
    ++n;
    if (c == std::string::npos) break;
    from = c + 1;
  }
  return n >= 2;
}

using BuiltinFn = std::function<Value(Interp&, List&)>;

}  // namespace

// ---- builtins ---------------------------------------------------------------
struct Builtins {
  static std::map<std::string, BuiltinFn>& Table() {
    static std::map<std::string, BuiltinFn> t = Make();
    return t;
  }

  static Value Arith(const char* name, char op, List& a) {
    if (a.empty()) { if (op == '+') return Int(0); if (op == '*') return Int(1); Fail(std::string("too few arguments to ") + name); }
    for (const Value& v : a) NeedNum(name, v);
    bool all_int = std::all_of(a.begin(), a.end(), [](const Value& v) { return v.t == Value::T::Int; });
    if (all_int) {
      long long acc = a[0].i;
      if (a.size() == 1 && op == '-') return Int(-acc);
      for (size_t k = 1; k < a.size(); ++k) {
        const long long x = a[k].i;
        switch (op) {
          case '+': acc += x; break;
          case '-': acc -= x; break;
          case '*': acc *= x; break;
          case '/': if (x == 0) Fail("divide by zero"); acc /= x; break;
        }
      }
      return Int(acc);
    }
    double acc = D(a[0]);
    if (a.size() == 1 && op == '-') return Real(-acc);
    for (size_t k = 1; k < a.size(); ++k) {
      const double x = D(a[k]);
      switch (op) {
        case '+': acc += x; break;
        case '-': acc -= x; break;
        case '*': acc *= x; break;
        case '/': if (x == 0) Fail("divide by zero"); acc /= x; break;
      }
    }
    return Real(acc);
  }

  static Value Cmp(const char* name, List& a, int kind) {
    if (a.size() < 2) Fail(std::string("too few arguments to ") + name);
    for (size_t k = 0; k + 1 < a.size(); ++k) {
      const Value& x = a[k];
      const Value& y = a[k + 1];
      bool ok;
      if (IsNum(x) && IsNum(y)) {
        const double p = D(x), q = D(y);
        ok = kind == 0 ? p == q : kind == 1 ? p != q : kind == 2 ? p < q : kind == 3 ? p > q : kind == 4 ? p <= q : p >= q;
      } else if (x.t == Value::T::Str && y.t == Value::T::Str) {
        const int c = x.s.compare(y.s);
        ok = kind == 0 ? c == 0 : kind == 1 ? c != 0 : kind == 2 ? c < 0 : kind == 3 ? c > 0 : kind == 4 ? c <= 0 : c >= 0;
      } else if (kind == 0 || kind == 1) {
        const bool eq = Equal(x, y);
        ok = kind == 0 ? eq : !eq;
      } else {
        Fail(std::string("bad argument type: numberp: ") + Interp::Format(IsNum(x) ? y : x, true) + " in " + name);
      }
      if (!ok) return Nil();
    }
    return True();
  }

  static Value Cxr(const char* name, const std::string& path, List& a) {
    NeedArgs(name, a, 1, 1);
    Value cur = a[0];
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
      const List& l = NeedList(name, cur);
      if (l.empty()) return Nil();
      if (*it == 'a') cur = l[0];
      else if (cur.dotted) cur = l[1];   // (cdr '(a . b)) is b
      else cur = MakeList(List(l.begin() + 1, l.end()));
    }
    return cur;
  }

  static Value TrimBoth(const char* name, List& a, bool left, bool right) {
    NeedArgs(name, a, 2, 2);
    const std::string& chars = NeedStr(name, a[0]);
    const std::string& s = NeedStr(name, a[1]);
    size_t b = 0, e = s.size();
    if (left) while (b < e && chars.find(s[b]) != std::string::npos) ++b;
    if (right) while (e > b && chars.find(s[e - 1]) != std::string::npos) --e;
    return Str(s.substr(b, e - b));
  }

  static Value SomeEvery(Interp& in, const char* name, List& a, bool some) {
    if (a.size() < 2) Fail(std::string("too few arguments to ") + name);
    size_t n = SIZE_MAX;
    for (size_t k = 1; k < a.size(); ++k) n = std::min(n, NeedList(name, a[k]).size());
    for (size_t i = 0; i < n; ++i) {
      List args;
      for (size_t k = 1; k < a.size(); ++k) args.push_back(Items(a[k])[i]);
      Value r = in.Apply(a[0], args);
      if (some && Truthy(r)) return r;
      if (!some && !Truthy(r)) return Nil();
    }
    return some ? Nil() : True();
  }

  // getint / getreal / getdist / getpoint / getkword / getstring: the prompt is printed, then the next
  // queued typed input is read (there is no blocking prompt: with nothing queued the result is nil).
  // initget bits: 1 no null input, 2 no zero, 4 no negative. Enter gives nil (getstring: ""), Cancel
  // aborts the program with "Function cancelled", input that does not parse is reported and the next
  // queued input is tried.
  static Value GetTyped(Interp& in, const char* name, List& a, char kind) {
    size_t at = 0;
    bool cr = false;
    if (kind == 's' && !a.empty() && a[0].t != Value::T::Str) { cr = Truthy(a[0]); at = 1; }   // (getstring [cr] [msg])
    if ((kind == 'p' || kind == 'd') && !a.empty() && IsList(a[0])) at = 1;                    // (getpoint [base] [msg])
    if (a.size() > at + 1) Fail(std::string("too many arguments to ") + name);
    std::string msg;
    if (a.size() == at + 1) msg = NeedStr(name, a[at]);
    (void)cr;
    const int bits = in.init_bits_;
    const std::string kws = in.init_kw_;
    in.init_bits_ = 0;
    in.init_kw_.clear();
    if (kind == 'k' && kws.empty()) { in.Emit("getkword: call initget with the keywords first\n"); return Nil(); }
    if (!msg.empty()) in.Emit(msg + "\n");
    for (;;) {
      std::optional<std::string> tok = in.host.next_input ? in.host.next_input() : std::nullopt;
      if (!tok) return Nil();
      const std::string& s = *tok;
      if (IsCancelTok(s)) Fail("Function cancelled");
      if (IsEnterTok(s)) {
        if (bits & 1) { in.Emit("Requires a value.\n"); continue; }
        return kind == 's' ? Str("") : Nil();
      }
      if (kind != 's') {
        if (std::optional<std::string> kw = MatchKeyword(kws, s)) return Str(*kw);
        if (kind == 'k') { in.Emit("Invalid option keyword.\n"); continue; }
      }
      long long iv = 0;
      double rv = 0, pv[3] = {0, 0, 0};
      switch (kind) {
        case 's': return Str(s);
        case 'i':
          if (!ParseWholeInt(s, iv)) { in.Emit("Requires an integer value.\n"); continue; }
          if ((bits & 2) && iv == 0) { in.Emit("Value must be nonzero.\n"); continue; }
          if ((bits & 4) && iv < 0) { in.Emit("Value must be positive.\n"); continue; }
          return Int(iv);
        case 'r': case 'd':
          if (!ParseWholeReal(s, rv)) { in.Emit(kind == 'r' ? "Requires numeric value.\n" : "Requires numeric distance or two points.\n"); continue; }
          if ((bits & 2) && rv == 0) { in.Emit("Value must be nonzero.\n"); continue; }
          if ((bits & 4) && rv < 0) { in.Emit("Value must be positive.\n"); continue; }
          return Real(rv);
        case 'p':
          if (!ParsePointText(s, pv)) { in.Emit("Point or option keyword required.\n"); continue; }
          return MakeList({Real(pv[0]), Real(pv[1]), Real(pv[2])});
        default: break;
      }
      return Nil();
    }
  }

  static std::map<std::string, BuiltinFn> Make() {
    std::map<std::string, BuiltinFn> t;
    t["+"] = [](Interp&, List& a) { return Arith("+", '+', a); };
    t["-"] = [](Interp&, List& a) { return Arith("-", '-', a); };
    t["*"] = [](Interp&, List& a) { return Arith("*", '*', a); };
    t["/"] = [](Interp&, List& a) { return Arith("/", '/', a); };
    t["="] = [](Interp&, List& a) { return Cmp("=", a, 0); };
    t["/="] = [](Interp&, List& a) { return Cmp("/=", a, 1); };
    t["<"] = [](Interp&, List& a) { return Cmp("<", a, 2); };
    t[">"] = [](Interp&, List& a) { return Cmp(">", a, 3); };
    t["<="] = [](Interp&, List& a) { return Cmp("<=", a, 4); };
    t[">="] = [](Interp&, List& a) { return Cmp(">=", a, 5); };
    t["1+"] = [](Interp&, List& a) { NeedArgs("1+", a, 1, 1); List b{a[0], Int(1)}; return Arith("1+", '+', b); };
    t["1-"] = [](Interp&, List& a) { NeedArgs("1-", a, 1, 1); List b{a[0], Int(1)}; return Arith("1-", '-', b); };
    t["abs"] = [](Interp&, List& a) {
      NeedArgs("abs", a, 1, 1); NeedNum("abs", a[0]);
      return a[0].t == Value::T::Int ? Int(std::llabs(a[0].i)) : Real(std::fabs(a[0].r));
    };
    t["sqrt"] = [](Interp&, List& a) { NeedArgs("sqrt", a, 1, 1); const double x = NeedNum("sqrt", a[0]); if (x < 0) Fail("function undefined for argument"); return Real(std::sqrt(x)); };
    t["sin"] = [](Interp&, List& a) { NeedArgs("sin", a, 1, 1); return Real(std::sin(NeedNum("sin", a[0]))); };
    t["cos"] = [](Interp&, List& a) { NeedArgs("cos", a, 1, 1); return Real(std::cos(NeedNum("cos", a[0]))); };
    t["atan"] = [](Interp&, List& a) {
      NeedArgs("atan", a, 1, 2);
      return a.size() == 1 ? Real(std::atan(NeedNum("atan", a[0]))) : Real(std::atan2(NeedNum("atan", a[0]), NeedNum("atan", a[1])));
    };
    t["exp"] = [](Interp&, List& a) { NeedArgs("exp", a, 1, 1); return Real(std::exp(NeedNum("exp", a[0]))); };
    t["log"] = [](Interp&, List& a) { NeedArgs("log", a, 1, 1); const double x = NeedNum("log", a[0]); if (x <= 0) Fail("function undefined for argument"); return Real(std::log(x)); };
    t["expt"] = [](Interp&, List& a) {
      NeedArgs("expt", a, 2, 2); NeedNum("expt", a[0]); NeedNum("expt", a[1]);
      if (a[0].t == Value::T::Int && a[1].t == Value::T::Int && a[1].i >= 0) {
        long long r = 1; for (long long k = 0; k < a[1].i; ++k) r *= a[0].i; return Int(r);
      }
      return Real(std::pow(D(a[0]), D(a[1])));
    };
    t["fix"] = [](Interp&, List& a) { NeedArgs("fix", a, 1, 1); return Int(static_cast<long long>(NeedNum("fix", a[0]))); };
    t["float"] = [](Interp&, List& a) { NeedArgs("float", a, 1, 1); return Real(NeedNum("float", a[0])); };
    t["min"] = [](Interp&, List& a) {
      if (a.empty()) Fail("too few arguments to min");
      Value m = a[0]; bool real = false;
      for (const Value& v : a) { NeedNum("min", v); real |= v.t == Value::T::Real; if (D(v) < D(m)) m = v; }
      return real ? Real(D(m)) : m;
    };
    t["max"] = [](Interp&, List& a) {
      if (a.empty()) Fail("too few arguments to max");
      Value m = a[0]; bool real = false;
      for (const Value& v : a) { NeedNum("max", v); real |= v.t == Value::T::Real; if (D(v) > D(m)) m = v; }
      return real ? Real(D(m)) : m;
    };
    t["rem"] = [](Interp&, List& a) {
      NeedArgs("rem", a, 2, 2); NeedNum("rem", a[0]); NeedNum("rem", a[1]);
      if (D(a[1]) == 0) Fail("divide by zero");
      if (a[0].t == Value::T::Int && a[1].t == Value::T::Int) return Int(a[0].i % a[1].i);
      return Real(std::fmod(D(a[0]), D(a[1])));
    };
    t["zerop"] = [](Interp&, List& a) { NeedArgs("zerop", a, 1, 1); return Bool(NeedNum("zerop", a[0]) == 0); };
    t["minusp"] = [](Interp&, List& a) { NeedArgs("minusp", a, 1, 1); return Bool(NeedNum("minusp", a[0]) < 0); };
    t["numberp"] = [](Interp&, List& a) { NeedArgs("numberp", a, 1, 1); return Bool(IsNum(a[0])); };
    t["null"] = [](Interp&, List& a) { NeedArgs("null", a, 1, 1); return Bool(IsNil(a[0])); };
    t["not"] = t["null"];
    t["listp"] = [](Interp&, List& a) { NeedArgs("listp", a, 1, 1); return Bool(IsList(a[0]) || IsNil(a[0])); };
    t["atom"] = [](Interp&, List& a) { NeedArgs("atom", a, 1, 1); return Bool(!IsList(a[0])); };
    t["eq"] = [](Interp&, List& a) {
      NeedArgs("eq", a, 2, 2);
      if (IsList(a[0]) || IsList(a[1])) return Bool(IsList(a[0]) && IsList(a[1]) && a[0].l == a[1].l);
      return Bool(Equal(a[0], a[1]) && (!IsNum(a[0]) || a[0].t == a[1].t));
    };
    t["equal"] = [](Interp&, List& a) { NeedArgs("equal", a, 2, 3); return Bool(Equal(a[0], a[1], a.size() == 3 ? NeedNum("equal", a[2]) : 0)); };
    t["type"] = [](Interp&, List& a) {
      NeedArgs("type", a, 1, 1);
      switch (a[0].t) {
        case Value::T::Nil: return Nil();
        case Value::T::True: case Value::T::Sym: return Sym("sym");
        case Value::T::Int: return Sym("int");
        case Value::T::Real: return Sym("real");
        case Value::T::Str: return Sym("str");
        case Value::T::List: return Sym(a[0].pickset ? "pickset" : "list");
        default: return Sym("subr");
      }
    };
    // lists
    t["list"] = [](Interp&, List& a) { return MakeList(a); };
    t["cons"] = [](Interp&, List& a) {
      NeedArgs("cons", a, 2, 2);
      if (!IsList(a[1]) && !IsNil(a[1])) return Pair(a[0], a[1]);   // (cons 'a 5) is the dotted pair (a . 5)
      if (a[1].dotted) Fail("cons onto a dotted pair is not supported");
      List l{a[0]}; for (const Value& v : Items(a[1])) l.push_back(v); return MakeList(l);
    };
    t["car"] = [](Interp&, List& a) { return Cxr("car", "a", a); };
    t["cdr"] = [](Interp&, List& a) { return Cxr("cdr", "d", a); };
    t["cadr"] = [](Interp&, List& a) { return Cxr("cadr", "ad", a); };
    t["cddr"] = [](Interp&, List& a) { return Cxr("cddr", "dd", a); };
    t["caddr"] = [](Interp&, List& a) { return Cxr("caddr", "add", a); };
    t["caar"] = [](Interp&, List& a) { return Cxr("caar", "aa", a); };
    t["append"] = [](Interp&, List& a) { List l; for (const Value& v : a) for (const Value& x : NeedList("append", v)) l.push_back(x); return MakeList(l); };
    t["reverse"] = [](Interp&, List& a) { NeedArgs("reverse", a, 1, 1); List l = NeedList("reverse", a[0]); std::reverse(l.begin(), l.end()); return MakeList(l); };
    t["length"] = [](Interp&, List& a) { NeedArgs("length", a, 1, 1); return Int(static_cast<long long>(NeedList("length", a[0]).size())); };
    t["nth"] = [](Interp&, List& a) {
      NeedArgs("nth", a, 2, 2);
      if (a[0].t != Value::T::Int || a[0].i < 0) Fail("bad argument type: wholenump");
      const List& l = NeedList("nth", a[1]);
      return static_cast<size_t>(a[0].i) < l.size() ? l[static_cast<size_t>(a[0].i)] : Nil();
    };
    t["last"] = [](Interp&, List& a) { NeedArgs("last", a, 1, 1); const List& l = NeedList("last", a[0]); return l.empty() ? Nil() : l.back(); };
    t["member"] = [](Interp&, List& a) {
      NeedArgs("member", a, 2, 2);
      const List& l = NeedList("member", a[1]);
      for (size_t k = 0; k < l.size(); ++k) if (Equal(a[0], l[k])) return MakeList(List(l.begin() + static_cast<long>(k), l.end()));
      return Nil();
    };
    t["assoc"] = [](Interp&, List& a) {
      NeedArgs("assoc", a, 2, 2);
      for (const Value& e : NeedList("assoc", a[1])) if (IsList(e) && !e.l->empty() && Equal(a[0], (*e.l)[0])) return e;
      return Nil();
    };
    t["apply"] = [](Interp& in, List& a) {
      NeedArgs("apply", a, 2, 2);
      return in.Apply(a[0], NeedList("apply", a[1]));
    };
    t["mapcar"] = [](Interp& in, List& a) {
      if (a.size() < 2) Fail("too few arguments to mapcar");
      size_t n = SIZE_MAX;
      for (size_t k = 1; k < a.size(); ++k) n = std::min(n, NeedList("mapcar", a[k]).size());
      List out;
      for (size_t i = 0; i < n; ++i) {
        List args;
        for (size_t k = 1; k < a.size(); ++k) args.push_back(Items(a[k])[i]);
        out.push_back(in.Apply(a[0], args));
      }
      return MakeList(out);
    };
    t["distance"] = [](Interp&, List& a) {
      NeedArgs("distance", a, 2, 2);
      const List& p = NeedList("distance", a[0]); const List& q = NeedList("distance", a[1]);
      if (p.size() < 2 || q.size() < 2) Fail("bad point argument");
      double s = 0;
      for (size_t k = 0; k < 3; ++k) { const double u = k < p.size() ? NeedNum("distance", p[k]) : 0, w = k < q.size() ? NeedNum("distance", q[k]) : 0; s += (u - w) * (u - w); }
      return Real(std::sqrt(s));
    };
    t["polar"] = [](Interp&, List& a) {
      NeedArgs("polar", a, 3, 3);
      const List& p = NeedList("polar", a[0]);
      if (p.size() < 2) Fail("bad point argument");
      const double ang = NeedNum("polar", a[1]), d = NeedNum("polar", a[2]);
      return MakeList({Real(NeedNum("polar", p[0]) + d * std::cos(ang)), Real(NeedNum("polar", p[1]) + d * std::sin(ang)), p.size() > 2 ? p[2] : Real(0)});
    };
    // strings
    t["strcat"] = [](Interp&, List& a) { std::string s; for (const Value& v : a) s += NeedStr("strcat", v); return Str(s); };
    t["strlen"] = [](Interp&, List& a) { long long n = 0; for (const Value& v : a) n += static_cast<long long>(NeedStr("strlen", v).size()); return Int(n); };
    t["substr"] = [](Interp&, List& a) {
      NeedArgs("substr", a, 2, 3);
      const std::string& s = NeedStr("substr", a[0]);
      if (a[1].t != Value::T::Int || a[1].i < 1) Fail("bad argument type: start must be a positive integer in substr");
      const size_t st = static_cast<size_t>(a[1].i) - 1;
      if (st >= s.size()) return Str("");
      size_t len = std::string::npos;
      if (a.size() == 3) { if (a[2].t != Value::T::Int || a[2].i < 0) Fail("bad argument type: length in substr"); len = static_cast<size_t>(a[2].i); }
      return Str(s.substr(st, len));
    };
    t["itoa"] = [](Interp&, List& a) { NeedArgs("itoa", a, 1, 1); if (a[0].t != Value::T::Int) Fail("bad argument type: fixnump in itoa"); return Str(std::to_string(a[0].i)); };
    t["atoi"] = [](Interp&, List& a) { NeedArgs("atoi", a, 1, 1); return Int(std::atoll(NeedStr("atoi", a[0]).c_str())); };
    t["atof"] = [](Interp&, List& a) { NeedArgs("atof", a, 1, 1); return Real(std::atof(NeedStr("atof", a[0]).c_str())); };
    t["rtos"] = [](Interp&, List& a) {
      NeedArgs("rtos", a, 1, 3);
      const double x = NeedNum("rtos", a[0]);
      int prec = 4;
      if (a.size() == 3) { if (a[2].t != Value::T::Int || a[2].i < 0 || a[2].i > 16) Fail("bad argument: precision in rtos"); prec = static_cast<int>(a[2].i); }
      if (a.size() >= 2 && (a[1].t != Value::T::Int || a[1].i != 2)) Fail("only rtos mode 2 (decimal) is supported");
      char buf[64]; std::snprintf(buf, sizeof buf, "%.*f", prec, x); return Str(buf);
    };
    t["strcase"] = [](Interp&, List& a) {
      NeedArgs("strcase", a, 1, 2);
      return Str(a.size() == 2 && Truthy(a[1]) ? Lower(NeedStr("strcase", a[0])) : Upper(NeedStr("strcase", a[0])));
    };
    // output
    t["princ"] = [](Interp& in, List& a) {
      if (a.empty()) return Nil();
      in.Emit(Interp::Format(a[0], false)); return a[0];
    };
    t["prin1"] = [](Interp& in, List& a) { if (a.empty()) return Nil(); in.Emit(Interp::Format(a[0], true)); return a[0]; };
    t["print"] = [](Interp& in, List& a) { NeedArgs("print", a, 1, 1); in.Emit("\n" + Interp::Format(a[0], true) + "\n"); return a[0]; };
    t["terpri"] = [](Interp& in, List&) { in.Emit("\n"); return Nil(); };
    t["prompt"] = [](Interp& in, List& a) { NeedArgs("prompt", a, 1, 1); in.Emit(NeedStr("prompt", a[0]) + "\n"); return Nil(); };
    t["alert"] = [](Interp& in, List& a) { NeedArgs("alert", a, 1, 1); in.Emit(NeedStr("alert", a[0]) + "\n"); return Nil(); };
    // evaluation
    t["eval"] = [](Interp& in, List& a) { NeedArgs("eval", a, 1, 1); return in.Eval(a[0]); };
    t["read"] = [](Interp&, List& a) {
      NeedArgs("read", a, 1, 1);
      Parser p(NeedStr("read", a[0]));
      if (p.AtEnd()) return Nil();
      return p.ParseOne();
    };
    t["load"] = [](Interp& in, List& a) {
      NeedArgs("load", a, 1, 2);
      const std::string& path = NeedStr("load", a[0]);
      std::string text;
      if (!in.host.read_file || !in.host.read_file(path, text)) Fail("can't open file: " + path);
      return in.EvalText(text);
    };
    // the drawing
    t["command"] = [](Interp& in, List& a) {
      if (a.empty()) Fail("too few arguments to command");
      if (!in.host.command) Fail("command is not available here");
      std::vector<std::string> parts;
      for (const Value& v : a) {
        if (v.t == Value::T::Str) parts.push_back(v.s);
        else if (v.t == Value::T::Int) parts.push_back(std::to_string(v.i));
        else if (v.t == Value::T::Real) { char b[64]; std::snprintf(b, sizeof b, "%.12g", v.r); parts.push_back(b); }
        else if (IsList(v)) {
          std::string p;
          for (size_t k = 0; k < v.l->size(); ++k) {
            const Value& c = (*v.l)[k];
            if (!IsNum(c)) Fail("bad argument type: point coordinates in command");
            char b[64]; std::snprintf(b, sizeof b, "%.12g", D(c)); p += (k ? "," : "") + std::string(b);
          }
          parts.push_back(p);
        } else if (IsNil(v)) {
          parts.push_back("");
        } else {
          Fail("bad argument type in command: " + Interp::Format(v, true));
        }
      }
      if (parts[0].empty()) Fail("the first argument of command must be a command name");
      in.FlushOutput();
      if (!in.host.command(parts)) Fail("command failed: " + parts[0]);
      return Nil();
    };
    t["entlast"] = [](Interp& in, List&) {
      if (!in.host.last_entity) return Nil();
      std::optional<long long> id = in.host.last_entity();
      return id ? Int(*id) : Nil();
    };
    t["ssget"] = [](Interp& in, List& a) {
      // (ssget "X" [filter]) every entity that passes the filter; (ssget "_L") the last entity.
      static const char* kOnly = "only (ssget \"X\") is supported";
      if (a.empty() || a.size() > 2 || a[0].t != Value::T::Str) Fail(kOnly);
      const std::string mode = Upper(a[0].s);
      if (mode == "_L" && a.size() == 1) {
        if (!in.host.last_entity) return Nil();
        std::optional<long long> id = in.host.last_entity();
        return id ? MakeSet({Int(*id)}) : Nil();
      }
      if (mode != "X") Fail(kOnly);
      if (!in.host.all_entities) return Nil();
      const bool filtered = a.size() == 2 && !IsNil(a[1]);
      if (filtered) NeedList("ssget", a[1]);
      if (filtered && !in.host.ent_get) Fail("entity access is not available here");
      List ids;
      for (long long id : in.host.all_entities()) {
        if (filtered) {
          std::vector<DxfItem> items;
          if (!in.host.ent_get(id, items)) continue;
          List ent;
          for (const DxfItem& d : items) ent.push_back(DxfToValue(d));
          if (!EntMatches(ent, Items(a[1]))) continue;
        }
        ids.push_back(Int(id));
      }
      return ids.empty() ? Nil() : MakeSet(ids);
    };
    t["sslength"] = [](Interp&, List& a) { NeedArgs("sslength", a, 1, 1); return Int(static_cast<long long>(NeedList("sslength", a[0]).size())); };
    t["ssname"] = [](Interp&, List& a) {
      NeedArgs("ssname", a, 2, 2);
      const List& l = NeedList("ssname", a[0]);
      if (a[1].t != Value::T::Int || a[1].i < 0) Fail("bad argument type: index in ssname");
      return static_cast<size_t>(a[1].i) < l.size() ? l[static_cast<size_t>(a[1].i)] : Nil();
    };
    // (ssadd) a new empty set; (ssadd e) a set holding e; (ssadd e ss) adds e to ss IN PLACE and returns ss.
    t["ssadd"] = [](Interp& in, List& a) {
      NeedArgs("ssadd", a, 0, 2);
      if (a.empty()) return MakeSet({});
      const long long id = NeedEname("ssadd", a[0]);
      if (in.host.ent_get) { std::vector<DxfItem> tmp; if (!in.host.ent_get(id, tmp)) return Nil(); }   // no such entity
      if (a.size() == 1) return MakeSet({Int(id)});
      if (a[1].t != Value::T::List || !a[1].pickset) Fail("bad argument type: pickp: " + Interp::Format(a[1], true) + " in ssadd");
      for (const Value& v : *a[1].l) if (v.t == Value::T::Int && v.i == id) return a[1];   // already a member
      a[1].l->push_back(Int(id));
      return a[1];
    };
    // (ssdel e ss) removes e from ss in place; nil when e was not in ss.
    t["ssdel"] = [](Interp&, List& a) {
      NeedArgs("ssdel", a, 2, 2);
      const long long id = NeedEname("ssdel", a[0]);
      if (a[1].t != Value::T::List || !a[1].pickset) Fail("bad argument type: pickp: " + Interp::Format(a[1], true) + " in ssdel");
      List& l = *a[1].l;
      for (size_t k = 0; k < l.size(); ++k) if (l[k].t == Value::T::Int && l[k].i == id) { l.erase(l.begin() + static_cast<long>(k)); return a[1]; }
      return Nil();
    };
    t["ssmemb"] = [](Interp&, List& a) {
      NeedArgs("ssmemb", a, 2, 2);
      const long long id = NeedEname("ssmemb", a[0]);
      for (const Value& v : NeedList("ssmemb", a[1])) if (v.t == Value::T::Int && v.i == id) return a[0];
      return Nil();
    };
    // entity access
    t["entget"] = [](Interp& in, List& a) {
      NeedArgs("entget", a, 1, 2);
      const long long id = NeedEname("entget", a[0]);
      std::vector<DxfItem> items;
      if (!in.host.ent_get || !in.host.ent_get(id, items)) return Nil();
      List out;
      for (const DxfItem& d : items) out.push_back(DxfToValue(d));
      return MakeList(out);
    };
    t["entnext"] = [](Interp& in, List& a) {
      NeedArgs("entnext", a, 0, 1);
      if (!in.host.ent_next) return Nil();
      std::optional<long long> after;
      if (a.size() == 1) after = NeedEname("entnext", a[0]);
      std::optional<long long> n = in.host.ent_next(after);
      return n ? Int(*n) : Nil();
    };
    t["entdel"] = [](Interp& in, List& a) {
      NeedArgs("entdel", a, 1, 1);
      const long long id = NeedEname("entdel", a[0]);
      return in.host.ent_del && in.host.ent_del(id) ? Int(id) : Nil();
    };
    // (entmod alist): applies the changed groups; returns alist, or nil when the entity or a change is refused.
    t["entmod"] = [](Interp& in, List& a) {
      NeedArgs("entmod", a, 1, 1);
      const std::vector<DxfItem> items = ValueToDxf(a[0]);
      long long id = 0;
      bool have = false;
      for (const DxfItem& d : items) if (d.code == -1 && d.k == DxfItem::K::Int) { id = d.i; have = true; }
      if (!have) Fail("entmod needs the (-1 . ename) group of the entity to change");
      if (!in.host.ent_mod) return Nil();
      const std::string why = in.host.ent_mod(id, items);
      if (!why.empty()) { in.Emit("entmod: " + why + "\n"); return Nil(); }
      return a[0];
    };
    // typed input: initget then getint / getreal / getpoint / getstring / getkword
    t["initget"] = [](Interp& in, List& a) {
      NeedArgs("initget", a, 0, 2);
      long long bits = 0;
      std::string kw;
      for (const Value& v : a) {
        if (v.t == Value::T::Int) bits = v.i;
        else if (v.t == Value::T::Str) kw = v.s;
        else if (!IsNil(v)) Fail("bad argument type in initget: " + Interp::Format(v, true));
      }
      in.init_bits_ = static_cast<int>(bits);
      in.init_kw_ = kw;
      return Nil();
    };
    t["getint"] = [](Interp& in, List& a) { return GetTyped(in, "getint", a, 'i'); };
    t["getreal"] = [](Interp& in, List& a) { return GetTyped(in, "getreal", a, 'r'); };
    t["getdist"] = [](Interp& in, List& a) { return GetTyped(in, "getdist", a, 'd'); };
    t["getpoint"] = [](Interp& in, List& a) { return GetTyped(in, "getpoint", a, 'p'); };
    t["getkword"] = [](Interp& in, List& a) { return GetTyped(in, "getkword", a, 'k'); };
    t["getstring"] = [](Interp& in, List& a) { return GetTyped(in, "getstring", a, 's'); };
    // strings and characters
    t["chr"] = [](Interp&, List& a) {
      NeedArgs("chr", a, 1, 1);
      if (a[0].t != Value::T::Int || a[0].i < 1 || a[0].i > 255) Fail("bad argument type: character code 1..255 in chr");
      return Str(std::string(1, static_cast<char>(a[0].i)));
    };
    t["ascii"] = [](Interp&, List& a) {
      NeedArgs("ascii", a, 1, 1);
      const std::string& s = NeedStr("ascii", a[0]);
      return Int(s.empty() ? 0 : static_cast<unsigned char>(s[0]));
    };
    t["subst"] = [](Interp&, List& a) {
      NeedArgs("subst", a, 3, 3);
      List l = NeedList("subst", a[2]);
      for (Value& v : l) if (Equal(v, a[1])) v = a[0];
      return MakeList(l);
    };
    // the vl-* subset
    t["vl-load-com"] = [](Interp&, List&) { return True(); };
    t["vl-list->string"] = [](Interp&, List& a) {
      NeedArgs("vl-list->string", a, 1, 1);
      std::string s;
      for (const Value& v : NeedList("vl-list->string", a[0])) {
        if (v.t != Value::T::Int || v.i < 1 || v.i > 255) Fail("bad argument type: character code 1..255 in vl-list->string");
        s += static_cast<char>(v.i);
      }
      return Str(s);
    };
    t["vl-string->list"] = [](Interp&, List& a) {
      NeedArgs("vl-string->list", a, 1, 1);
      List l;
      for (unsigned char c : NeedStr("vl-string->list", a[0])) l.push_back(Int(c));
      return MakeList(l);
    };
    t["vl-string-search"] = [](Interp&, List& a) {
      NeedArgs("vl-string-search", a, 2, 3);
      const std::string& pat = NeedStr("vl-string-search", a[0]);
      const std::string& s = NeedStr("vl-string-search", a[1]);
      long long from = 0;
      if (a.size() == 3) { if (a[2].t != Value::T::Int || a[2].i < 0) Fail("bad argument type: start in vl-string-search"); from = a[2].i; }
      if (static_cast<size_t>(from) > s.size()) return Nil();
      const size_t at = s.find(pat, static_cast<size_t>(from));
      return at == std::string::npos ? Nil() : Int(static_cast<long long>(at));
    };
    t["vl-string-subst"] = [](Interp&, List& a) {
      NeedArgs("vl-string-subst", a, 3, 4);
      const std::string& rep = NeedStr("vl-string-subst", a[0]);
      const std::string& pat = NeedStr("vl-string-subst", a[1]);
      std::string s = NeedStr("vl-string-subst", a[2]);
      long long from = 0;
      if (a.size() == 4) { if (a[3].t != Value::T::Int || a[3].i < 0) Fail("bad argument type: start in vl-string-subst"); from = a[3].i; }
      if (pat.empty() || static_cast<size_t>(from) > s.size()) return Str(s);
      const size_t at = s.find(pat, static_cast<size_t>(from));
      if (at != std::string::npos) s.replace(at, pat.size(), rep);
      return Str(s);
    };
    t["vl-string-position"] = [](Interp&, List& a) {
      NeedArgs("vl-string-position", a, 2, 4);
      if (a[0].t != Value::T::Int || a[0].i < 0 || a[0].i > 255) Fail("bad argument type: character code in vl-string-position");
      const std::string& s = NeedStr("vl-string-position", a[1]);
      long long from = 0;
      if (a.size() >= 3 && !IsNil(a[2])) { if (a[2].t != Value::T::Int || a[2].i < 0) Fail("bad argument type: start in vl-string-position"); from = a[2].i; }
      const bool last = a.size() == 4 && Truthy(a[3]);
      const char ch = static_cast<char>(a[0].i);
      if (last) {
        for (long long k = static_cast<long long>(s.size()) - 1; k >= from; --k) if (s[static_cast<size_t>(k)] == ch) return Int(k);
        return Nil();
      }
      const size_t at = static_cast<size_t>(from) <= s.size() ? s.find(ch, static_cast<size_t>(from)) : std::string::npos;
      return at == std::string::npos ? Nil() : Int(static_cast<long long>(at));
    };
    t["vl-string-trim"] = [](Interp&, List& a) { return TrimBoth("vl-string-trim", a, true, true); };
    t["vl-string-left-trim"] = [](Interp&, List& a) { return TrimBoth("vl-string-left-trim", a, true, false); };
    t["vl-string-right-trim"] = [](Interp&, List& a) { return TrimBoth("vl-string-right-trim", a, false, true); };
    t["vl-princ-to-string"] = [](Interp&, List& a) { NeedArgs("vl-princ-to-string", a, 1, 1); return Str(Interp::Format(a[0], false)); };
    t["vl-prin1-to-string"] = [](Interp&, List& a) { NeedArgs("vl-prin1-to-string", a, 1, 1); return Str(Interp::Format(a[0], true)); };
    t["vl-position"] = [](Interp&, List& a) {
      NeedArgs("vl-position", a, 2, 2);
      const List& l = NeedList("vl-position", a[1]);
      for (size_t k = 0; k < l.size(); ++k) if (Equal(a[0], l[k])) return Int(static_cast<long long>(k));
      return Nil();
    };
    t["vl-remove"] = [](Interp&, List& a) {
      NeedArgs("vl-remove", a, 2, 2);
      List out;
      for (const Value& v : NeedList("vl-remove", a[1])) if (!Equal(a[0], v)) out.push_back(v);
      return MakeList(out);
    };
    t["vl-remove-if"] = [](Interp& in, List& a) {
      NeedArgs("vl-remove-if", a, 2, 2);
      List out;
      const List src = NeedList("vl-remove-if", a[1]);
      for (const Value& v : src) if (!Truthy(in.Apply(a[0], {v}))) out.push_back(v);
      return MakeList(out);
    };
    t["vl-remove-if-not"] = [](Interp& in, List& a) {
      NeedArgs("vl-remove-if-not", a, 2, 2);
      List out;
      const List src = NeedList("vl-remove-if-not", a[1]);
      for (const Value& v : src) if (Truthy(in.Apply(a[0], {v}))) out.push_back(v);
      return MakeList(out);
    };
    t["vl-member-if"] = [](Interp& in, List& a) {
      NeedArgs("vl-member-if", a, 2, 2);
      const List src = NeedList("vl-member-if", a[1]);
      for (size_t k = 0; k < src.size(); ++k) if (Truthy(in.Apply(a[0], {src[k]}))) return MakeList(List(src.begin() + static_cast<long>(k), src.end()));
      return Nil();
    };
    // (vl-sort list less-p): stable merge sort driven only by (less-p b a), so a sloppy predicate cannot crash it.
    t["vl-sort"] = [](Interp& in, List& a) {
      NeedArgs("vl-sort", a, 2, 2);
      List cur = NeedList("vl-sort", a[0]);
      const size_t n = cur.size();
      for (size_t w = 1; w < n; w *= 2) {
        List next;
        for (size_t lo = 0; lo < n; lo += 2 * w) {
          const size_t mid = std::min(lo + w, n), hi = std::min(lo + 2 * w, n);
          size_t i = lo, j = mid;
          while (i < mid && j < hi) {
            if (Truthy(in.Apply(a[1], {cur[j], cur[i]}))) next.push_back(cur[j++]); else next.push_back(cur[i++]);
          }
          while (i < mid) next.push_back(cur[i++]);
          while (j < hi) next.push_back(cur[j++]);
        }
        cur = std::move(next);
      }
      return MakeList(cur);
    };
    // (vl-some pred list ...) the first non-nil result; (vl-every pred list ...) T when every result is non-nil.
    t["vl-some"] = [](Interp& in, List& a) { return SomeEvery(in, "vl-some", a, true); };
    t["vl-every"] = [](Interp& in, List& a) { return SomeEvery(in, "vl-every", a, false); };
    return t;
  }
};

// ---- interpreter ------------------------------------------------------------
Interp::Interp() {
  globals_["pi"] = Real(3.14159265358979323846);
}

List Interp::ParseAll(const std::string& text) {
  Parser p(text);
  List out;
  while (!p.AtEnd()) out.push_back(p.ParseOne());
  return out;
}

void Interp::SplitInput(const std::string& text, std::string& forms, std::string& rest) {
  forms = text;
  rest.clear();
  Parser p(text);
  p.SkipSpace();
  if (p.pos >= text.size() || text[p.pos] != '(') return;
  size_t end = 0;
  try {
    for (;;) {
      p.SkipSpace();
      if (p.pos >= text.size() || text[p.pos] != '(') break;   // typed input starts at the first non-list
      p.ParseOne();
      end = p.pos;
    }
  } catch (const LispError&) {
    return;   // never balances: the evaluator reports the syntax error
  }
  forms = text.substr(0, end);
  rest = text.substr(end);
}

void Interp::Emit(const std::string& text) {
  out_buf_ += text;
  size_t nl;
  while ((nl = out_buf_.find('\n')) != std::string::npos) {
    const std::string line = out_buf_.substr(0, nl);
    out_buf_.erase(0, nl + 1);
    if (host.print && !line.empty()) host.print(line);
  }
}

void Interp::FlushOutput() {
  if (!out_buf_.empty()) {
    if (host.print) host.print(out_buf_);
    out_buf_.clear();
  }
}

Value Interp::EvalText(const std::string& text) {
  List forms = ParseAll(text);   // a syntax error anywhere runs nothing
  const bool top = depth_ == 0;
  if (top) steps_ = 0;
  Value last = Nil();
  for (const Value& f : forms) last = Eval(f);
  return last;
}

bool Interp::IsDefined(const std::string& name) const {
  auto it = globals_.find(Lower(name));
  return it != globals_.end() && it->second.t == Value::T::Lambda;
}

Value Interp::CallNamed(const std::string& name) {
  auto it = globals_.find(Lower(name));
  if (it == globals_.end() || it->second.t != Value::T::Lambda) Fail("no function definition: " + Upper(name));
  if (depth_ == 0) steps_ = 0;
  return Apply(it->second, {});
}

Value Interp::Eval(const Value& v) {
  if (++steps_ > kMaxSteps) Fail("evaluation step limit exceeded (possible endless loop)");
  switch (v.t) {
    case Value::T::Sym: {
      auto it = globals_.find(v.s);
      if (it != globals_.end()) return it->second;
      return Nil();
    }
    case Value::T::List: return EvalList(v);
    default: return v;
  }
}

Value Interp::CallUser(const Func& f, List args) {
  if (args.size() > f.params.size()) Fail("too many arguments");
  struct Saved { std::string name; bool had; Value old; };
  std::vector<Saved> saved;
  auto bind = [&](const std::string& n, Value val) {
    auto it = globals_.find(n);
    saved.push_back({n, it != globals_.end(), it != globals_.end() ? it->second : Nil()});
    globals_[n] = std::move(val);
  };
  for (size_t k = 0; k < f.params.size(); ++k) bind(f.params[k], k < args.size() ? args[k] : Nil());
  for (const std::string& n : f.locals) bind(n, Nil());
  struct Restore {
    std::map<std::string, Value>& g; std::vector<Saved>& s;
    ~Restore() {
      for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (it->had) g[it->name] = it->old; else g.erase(it->name);
      }
    }
  } restore{globals_, saved};
  if (++depth_ > kMaxDepth) { --depth_; Fail("recursion too deep"); }
  struct Depth { int& d; ~Depth() { --d; } } dd{depth_};
  Value r = Nil();
  for (const Value& b : f.body) r = Eval(b);
  return r;
}

Value Interp::Apply(const Value& fn, List args) {
  if (fn.t == Value::T::Lambda) return CallUser(*fn.f, std::move(args));
  // '(lambda (x) ...) arrives quoted as a plain list: AutoLISP accepts it as a function.
  if (IsList(fn) && !fn.l->empty() && (*fn.l)[0].t == Value::T::Sym && (*fn.l)[0].s == "lambda") return Apply(Eval(fn), std::move(args));
  if (fn.t == Value::T::Sym || fn.t == Value::T::Subr) {
    auto it = globals_.find(fn.s);
    if (it != globals_.end() && it->second.t == Value::T::Lambda) return CallUser(*it->second.f, std::move(args));
    auto b = Builtins::Table().find(fn.s);
    if (b != Builtins::Table().end()) return b->second(*this, args);
    Fail("no function definition: " + Upper(fn.s));
  }
  Fail("bad function: " + Format(fn, true));
}

Value Interp::EvalList(const Value& form) {
  const List& items = *form.l;
  const Value& head = items[0];
  if (head.t == Value::T::List) {
    // ((lambda ...) args)
    Value fn = Eval(head);
    List args;
    for (size_t k = 1; k < items.size(); ++k) args.push_back(Eval(items[k]));
    return Apply(fn, args);
  }
  if (head.t != Value::T::Sym) Fail("bad function: " + Format(head, true));
  const std::string& n = head.s;
  // special forms (these names cannot be redefined)
  if (n == "quote") { if (items.size() != 2) Fail("quote takes one argument"); return items[1]; }
  if (n == "function") { if (items.size() != 2) Fail("function takes one argument"); return items[1].t == Value::T::List ? Eval(items[1]) : items[1]; }
  if (n == "setq") {
    if (items.size() < 3 || items.size() % 2 == 0) Fail("setq needs symbol/value pairs");
    Value last = Nil();
    for (size_t k = 1; k + 1 < items.size(); k += 2) {
      if (items[k].t != Value::T::Sym) Fail("bad argument type: symbolp: " + Format(items[k], true));
      if (Builtins::Table().count(items[k].s) && items[k].s != "pi") {
        // AutoLISP lets a user rebind a built-in name; here that would hide the function, so refuse.
        Fail("cannot redefine the built-in function " + Upper(items[k].s));
      }
      last = Eval(items[k + 1]);
      globals_[items[k].s] = last;
    }
    return last;
  }
  if (n == "if") {
    if (items.size() < 3 || items.size() > 4) Fail("if takes a test, a then form and an optional else form");
    if (Truthy(Eval(items[1]))) return Eval(items[2]);
    return items.size() == 4 ? Eval(items[3]) : Nil();
  }
  if (n == "progn") { Value r = Nil(); for (size_t k = 1; k < items.size(); ++k) r = Eval(items[k]); return r; }
  if (n == "and") { Value r = True(); for (size_t k = 1; k < items.size(); ++k) { r = Eval(items[k]); if (!Truthy(r)) return Nil(); } return r; }
  if (n == "or") { for (size_t k = 1; k < items.size(); ++k) { Value r = Eval(items[k]); if (Truthy(r)) return r; } return Nil(); }
  if (n == "cond") {
    for (size_t k = 1; k < items.size(); ++k) {
      if (!IsList(items[k])) Fail("bad cond clause");
      const List& cl = *items[k].l;
      Value t = Eval(cl[0]);
      if (Truthy(t)) { for (size_t j = 1; j < cl.size(); ++j) t = Eval(cl[j]); return t; }
    }
    return Nil();
  }
  if (n == "while") {
    if (items.size() < 2) Fail("while needs a test");
    while (Truthy(Eval(items[1]))) for (size_t k = 2; k < items.size(); ++k) Eval(items[k]);
    return Nil();
  }
  if (n == "repeat") {
    if (items.size() < 2) Fail("repeat needs a count");
    Value c = Eval(items[1]);
    if (c.t != Value::T::Int) Fail("bad argument type: fixnump: " + Format(c, true));
    Value r = Nil();
    for (long long k = 0; k < c.i; ++k) for (size_t j = 2; j < items.size(); ++j) r = Eval(items[j]);
    return r;
  }
  if (n == "foreach") {
    if (items.size() < 3 || items[1].t != Value::T::Sym) Fail("foreach needs a symbol and a list");
    Value lst = Eval(items[2]);
    if (!IsList(lst) && !IsNil(lst)) Fail("bad argument type: listp: " + Format(lst, true));
    const std::string var = items[1].s;
    auto it = globals_.find(var);
    const bool had = it != globals_.end();
    Value old = had ? it->second : Nil();
    struct R { std::map<std::string, Value>& g; std::string v; bool had; Value old; ~R() { if (had) g[v] = old; else g.erase(v); } } rr{globals_, var, had, old};
    Value r = Nil();
    const List copy = Items(lst);
    for (const Value& e : copy) { globals_[var] = e; for (size_t k = 3; k < items.size(); ++k) r = Eval(items[k]); }
    return r;
  }
  if (n == "lambda") {
    if (items.size() < 2 || !(IsList(items[1]) || IsNil(items[1]))) Fail("lambda needs a parameter list");
    auto f = std::make_shared<Func>();
    bool locals = false;
    for (const Value& p : Items(items[1])) {
      if (p.t != Value::T::Sym) Fail("bad parameter list");
      if (p.s == "/") locals = true; else (locals ? f->locals : f->params).push_back(p.s);
    }
    for (size_t k = 2; k < items.size(); ++k) f->body.push_back(items[k]);
    Value v; v.t = Value::T::Lambda; v.f = f; return v;
  }
  if (n == "defun") {
    if (items.size() < 3 || items[1].t != Value::T::Sym) Fail("defun needs a name and a parameter list");
    const std::string name = items[1].s;
    if (Builtins::Table().count(name) || name == "quote" || name == "setq" || name == "if" || name == "progn" || name == "and" || name == "or" ||
        name == "cond" || name == "while" || name == "repeat" || name == "foreach" || name == "lambda" || name == "defun" || name == "function")
      Fail("cannot redefine the built-in function " + Upper(name));
    Value lam = EvalList(MakeList([&] { List l{Sym("lambda")}; for (size_t k = 2; k < items.size(); ++k) l.push_back(items[k]); return l; }()));
    // The host may refuse a c: name (it would replace a built-in command): then nothing is defined.
    if (name.rfind("c:", 0) == 0 && host.on_define_command) host.on_define_command(name);
    globals_[name] = lam;
    return Sym(name);
  }
  // ordinary call: a user function wins over nothing else (built-ins cannot be redefined)
  List args;
  args.reserve(items.size() - 1);
  for (size_t k = 1; k < items.size(); ++k) args.push_back(Eval(items[k]));
  auto b = Builtins::Table().find(n);
  if (b != Builtins::Table().end()) return b->second(*this, args);
  auto g = globals_.find(n);
  if (g != globals_.end() && g->second.t == Value::T::Lambda) return CallUser(*g->second.f, std::move(args));
  Fail("no function definition: " + Upper(n));
}

std::string Interp::Format(const Value& v, bool quote_strings) {
  switch (v.t) {
    case Value::T::Nil: return "nil";
    case Value::T::True: return "T";
    case Value::T::Int: return std::to_string(v.i);
    case Value::T::Real: return FormatReal(v.r);
    case Value::T::Str: {
      if (!quote_strings) return v.s;
      std::string o = "\"";
      for (char c : v.s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else o += c;
      }
      return o + "\"";
    }
    case Value::T::Sym: return Upper(v.s);
    case Value::T::List: {
      if (v.pickset) return "<Selection set: " + std::to_string(v.l->size()) + ">";
      if (v.dotted) return "(" + Format((*v.l)[0], quote_strings) + " . " + Format((*v.l)[1], quote_strings) + ")";
      std::string o = "(";
      for (size_t k = 0; k < v.l->size(); ++k) { if (k) o += " "; o += Format((*v.l)[k], quote_strings); }
      return o + ")";
    }
    case Value::T::Subr: return "#<SUBR @" + v.s + ">";
    case Value::T::Lambda: return "#<USUBR>";
  }
  return "";
}

}  // namespace dino8::lisp
