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
long long NeedInt(const char* fn, const Value& v) {
  if (v.t != Value::T::Int) Fail(std::string("bad argument type: integerp: ") + Interp::Format(v, true) + " in " + fn);
  return v.i;
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
    case Value::T::Vla: return a.s == b.s && a.i == b.i;
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
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {   // \nnn: a character by its octal code
              int code = e - '0';
              for (int k = 0; k < 2 && pos < src.size() && src[pos] >= '0' && src[pos] <= '7'; ++k) code = code * 8 + (src[pos++] - '0');
              out += static_cast<char>(code);
              break;
            }
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
    const bool numeric_chars = tok.find_first_not_of("0123456789+-.eE") == std::string::npos;   // no hex, inf or nan
    const double dv = std::strtod(tok.c_str(), &end);
    if (numeric_chars && end && *end == 0 && (std::isdigit(static_cast<unsigned char>(tok.back())) || tok.back() == '.')) return Real(dv);
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
std::vector<DxfItem> ValueToDxf(const char* fn, const Value& alist) {
  std::vector<DxfItem> out;
  for (const Value& e : NeedList(fn, alist)) {
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

// ---- ssget filters with -4 operators, windowed selection ------------------------------
bool RelOk(const std::string& op, const Value& have, const Value& want) {
  if (op == "*") return true;
  if (have.t == Value::T::Str && want.t == Value::T::Str) {
    if (op == "=") return WildMatch(want.s, have.s);
    if (op == "/=") return !WildMatch(want.s, have.s);
    Fail("ssget filter: the relational operator \"" + op + "\" compares numbers, not strings");
  }
  if (IsNum(have) && IsNum(want)) {
    const double a = D(have), b = D(want), eps = 1e-9;
    if (op == "=") return std::fabs(a - b) <= eps;
    if (op == "/=") return std::fabs(a - b) > eps;
    if (op == "<") return a < b - eps;
    if (op == ">") return a > b + eps;
    if (op == "<=") return a <= b + eps;
    if (op == ">=") return a >= b - eps;
    Fail("ssget filter: unknown relational operator \"" + op + "\"");
  }
  return false;
}

// One (code . value) or (code x y z) filter entry against the entity, under relational operator `op`
// ("=", "/=", "<", ">", "<=", ">=", "*", or for a point three of them joined by commas).
bool PairOk(const List& ent, const Value& f, const std::string& op) {
  if (!IsList(f) || f.l->empty() || (*f.l)[0].t != Value::T::Int) Fail("bad ssget filter: every entry must be (code . value)");
  const long long code = (*f.l)[0].i;
  const bool scalar = f.dotted || f.l->size() == 2;
  std::vector<std::string> ops;
  {
    size_t from = 0;
    for (;;) {
      const size_t c = op.find(',', from);
      ops.push_back(op.substr(from, c == std::string::npos ? std::string::npos : c - from));
      if (c == std::string::npos) break;
      from = c + 1;
    }
  }
  if (scalar && ops.size() != 1) Fail("ssget filter: a comma operator \"" + op + "\" needs a point group");
  if (!scalar && ops.size() != 1 && ops.size() != f.l->size() - 1) Fail("ssget filter: the operator \"" + op + "\" needs one part per coordinate");
  bool saw = false;
  for (const Value& e : ent) {
    if (!IsList(e) || e.l->empty() || (*e.l)[0].t != Value::T::Int || (*e.l)[0].i != code) continue;
    saw = true;
    if (scalar) {
      if (RelOk(ops[0], (*e.l)[1], (*f.l)[1])) return true;
    } else {
      if (e.l->size() < f.l->size()) continue;
      bool all = true;
      for (size_t k = 1; k < f.l->size() && all; ++k) all = RelOk(ops.size() == 1 ? ops[0] : ops[k - 1], (*e.l)[k], (*f.l)[k]);
      if (all) return true;
    }
  }
  // A BYLAYER entity carries no group 62; it counts as 256 (BYLAYER).
  if (!saw && code == 62 && scalar) return RelOk(ops[0], Int(256), (*f.l)[1]);
  return false;
}

bool EvalItem(const List& ent, const List& f, size_t& i);

// Evaluates the items from i up to the closing token `closer` (consumed), or to the end when `closer` is empty.
std::vector<bool> EvalItems(const List& ent, const List& f, size_t& i, const std::string& closer) {
  std::vector<bool> r;
  while (i < f.size()) {
    const Value& e = f[i];
    if (!closer.empty() && IsList(e) && e.l->size() == 2 && (*e.l)[0].t == Value::T::Int && (*e.l)[0].i == -4 &&
        (*e.l)[1].t == Value::T::Str && Upper((*e.l)[1].s) == closer) {
      ++i;
      return r;
    }
    r.push_back(EvalItem(ent, f, i));
  }
  if (!closer.empty()) Fail("ssget filter: the group is not closed (missing " + closer + ")");
  return r;
}

bool EvalItem(const List& ent, const List& f, size_t& i) {
  const Value& e = f[i];
  if (!IsList(e) || e.l->empty() || (*e.l)[0].t != Value::T::Int) Fail("bad ssget filter: every entry must be (code . value)");
  if ((*e.l)[0].i != -4) { ++i; return PairOk(ent, e, "="); }
  if (e.l->size() != 2 || (*e.l)[1].t != Value::T::Str) Fail("bad ssget filter: (-4 . \"operator\") needs a string");
  const std::string op = Upper((*e.l)[1].s);
  ++i;
  if (op == "<AND" || op == "<OR" || op == "<XOR" || op == "<NOT") {
    const std::string closer = op.substr(1) + ">";
    const std::vector<bool> r = EvalItems(ent, f, i, closer);
    size_t n = 0;
    for (bool b : r) n += b ? 1 : 0;
    if (op == "<AND") return n == r.size();
    if (op == "<OR") return n > 0;
    if (op == "<XOR") return n % 2 == 1;
    return n != r.size() || r.empty();   // <NOT: true unless every operand holds (an empty group is true)
  }
  if (op == "AND>" || op == "OR>" || op == "XOR>" || op == "NOT>") Fail("ssget filter: " + op + " without its opening " + "<" + op.substr(0, op.size() - 1));
  if (i >= f.size()) Fail("ssget filter: the operator \"" + op + "\" needs a group after it");
  if (IsList(f[i]) && !f[i].l->empty() && (*f[i].l)[0].t == Value::T::Int && (*f[i].l)[0].i == -4) Fail("ssget filter: the operator \"" + op + "\" needs a group after it, not another operator");
  const Value& g = f[i];
  ++i;
  return PairOk(ent, g, op);
}

bool EntMatches(const List& ent, const List& filter) {
  size_t i = 0;
  const std::vector<bool> r = EvalItems(ent, filter, i, "");
  for (bool b : r) if (!b) return false;
  return true;
}

using P2 = std::array<double, 2>;
using Outline = std::vector<std::vector<P2>>;

double Orient(P2 a, P2 b, P2 c) { return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); }
bool OnSeg(P2 a, P2 b, P2 p) {
  const double eps = 1e-9;
  return std::fabs(Orient(a, b, p)) <= eps * (1 + std::fabs(b[0] - a[0]) + std::fabs(b[1] - a[1])) &&
         p[0] >= std::min(a[0], b[0]) - eps && p[0] <= std::max(a[0], b[0]) + eps &&
         p[1] >= std::min(a[1], b[1]) - eps && p[1] <= std::max(a[1], b[1]) + eps;
}
// Segments ab and cd share a point (touching counts).
bool SegTouch(P2 a, P2 b, P2 c, P2 d) {
  const double o1 = Orient(a, b, c), o2 = Orient(a, b, d), o3 = Orient(c, d, a), o4 = Orient(c, d, b);
  if (((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) && ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0))) return true;
  return OnSeg(a, b, c) || OnSeg(a, b, d) || OnSeg(c, d, a) || OnSeg(c, d, b);
}
// Segments cross at a point interior to both (no touching, no sharing an end).
bool SegCrossStrict(P2 a, P2 b, P2 c, P2 d) {
  const double o1 = Orient(a, b, c), o2 = Orient(a, b, d), o3 = Orient(c, d, a), o4 = Orient(c, d, b);
  return ((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) && ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0));
}
// Inside or on the boundary of the closed polygon.
bool InPoly(const std::vector<P2>& poly, P2 p) {
  bool in = false;
  const size_t n = poly.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    if (OnSeg(poly[j], poly[i], p)) return true;
    if ((poly[i][1] > p[1]) != (poly[j][1] > p[1]) &&
        p[0] < (poly[j][0] - poly[i][0]) * (p[1] - poly[i][1]) / (poly[j][1] - poly[i][1]) + poly[i][0])
      in = !in;
  }
  return in;
}

// mode 'W': every part of the entity inside the polygon; 'C': some part inside or crossing; 'F': crosses the open polyline.
bool OutlineSelected(const Outline& out, const std::vector<P2>& poly, char mode) {
  const size_t n = poly.size();
  const size_t edges = mode == 'F' ? n - 1 : n;
  auto edge = [&](size_t k) { return std::make_pair(poly[k], poly[(k + 1) % n]); };
  if (mode == 'W') {
    for (const auto& pl : out) {
      for (const P2& p : pl) if (!InPoly(poly, p)) return false;
      for (size_t s = 0; s + 1 < pl.size(); ++s)
        for (size_t k = 0; k < edges; ++k) { auto e = edge(k); if (SegCrossStrict(pl[s], pl[s + 1], e.first, e.second)) return false; }
    }
    return !out.empty();
  }
  for (const auto& pl : out) {
    if (mode == 'C') for (const P2& p : pl) if (InPoly(poly, p)) return true;
    for (size_t s = 0; s + 1 < pl.size(); ++s)
      for (size_t k = 0; k < edges; ++k) { auto e = edge(k); if (SegTouch(pl[s], pl[s + 1], e.first, e.second)) return true; }
  }
  return false;
}

P2 NeedPt2(const char* fn, const Value& v) {
  const List& l = NeedList(fn, v);
  if (l.size() < 2 || !IsNum(l[0]) || !IsNum(l[1])) Fail(std::string("bad point in ") + fn + ": a list of two or three numbers is needed");
  return {D(l[0]), D(l[1])};
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

// ---- system variables, symbol tables, and the VLA object model over the entity model ------------------------------
Value ItemValue(const DxfItem& d) {
  switch (d.k) {
    case DxfItem::K::Str: return Str(d.s);
    case DxfItem::K::Int: return Int(d.i);
    case DxfItem::K::Real: return Real(d.x);
    case DxfItem::K::Point: return MakeList({Real(d.x), Real(d.y), Real(d.z)});
  }
  return Nil();
}
DxfItem ValueItem(const char* fn, const Value& v) {
  DxfItem d;
  if (v.t == Value::T::Str) { d.k = DxfItem::K::Str; d.s = v.s; }
  else if (v.t == Value::T::Int) { d.k = DxfItem::K::Int; d.i = v.i; d.x = static_cast<double>(v.i); }
  else if (v.t == Value::T::Real) { d.k = DxfItem::K::Real; d.x = v.r; }
  else if (IsList(v) && (v.l->size() == 2 || v.l->size() == 3) && IsNum((*v.l)[0]) && IsNum((*v.l)[1]) && (v.l->size() == 2 || IsNum((*v.l)[2]))) {
    d.k = DxfItem::K::Point; d.x = D((*v.l)[0]); d.y = D((*v.l)[1]); d.z = v.l->size() == 3 ? D((*v.l)[2]) : 0;
  } else {
    Fail(std::string("bad argument type in ") + fn + ": a string, an integer, a real or a point is needed");
  }
  return d;
}

using P3 = std::array<double, 3>;
constexpr double kTwoPi = 6.283185307179586;

Value MakeVla(const char* kind, long long id = 0) { Value v; v.t = Value::T::Vla; v.s = kind; v.i = id; return v; }
Value Pt3Value(P3 p) { return MakeList({Real(p[0]), Real(p[1]), Real(p[2])}); }
P3 NeedP3(const char* fn, const Value& v) {
  if (!IsList(v) || (v.l->size() != 2 && v.l->size() != 3)) Fail(std::string("bad point in ") + fn + ": a list of two or three numbers is needed");
  P3 p{0, 0, 0};
  for (size_t k = 0; k < v.l->size(); ++k) { if (!IsNum((*v.l)[k])) Fail(std::string("bad point in ") + fn + ": coordinates must be numbers"); p[k] = D((*v.l)[k]); }
  return p;
}

const DxfItem* GroupOf(const std::vector<DxfItem>& g, int code) {
  for (const DxfItem& d : g) if (d.code == code) return &d;
  return nullptr;
}
P3 GroupPt(const std::vector<DxfItem>& g, int code) { const DxfItem* d = GroupOf(g, code); return d ? P3{d->x, d->y, d->z} : P3{0, 0, 0}; }
double GroupReal(const std::vector<DxfItem>& g, int code, double def = 0) { const DxfItem* d = GroupOf(g, code); return d ? d->x : def; }
std::string GroupStr(const std::vector<DxfItem>& g, int code) { const DxfItem* d = GroupOf(g, code); return d ? d->s : ""; }
std::vector<P3> GroupPts(const std::vector<DxfItem>& g, int code) {
  std::vector<P3> r;
  for (const DxfItem& d : g) if (d.code == code && d.k == DxfItem::K::Point) r.push_back({d.x, d.y, d.z});
  return r;
}

long long NeedEntId(const char* fn, const Value& v) {
  if (v.t == Value::T::Int) return v.i;
  if (v.t == Value::T::Vla && v.s == "ent") return v.i;
  Fail(std::string("bad argument type: ename or VLA entity: ") + Interp::Format(v, true) + " in " + fn);
}

std::vector<DxfItem> LoadEnt(Interp& in, long long id) {
  std::vector<DxfItem> g;
  if (!in.host.ent_get || !in.host.ent_get(id, g)) Fail("the object has been erased or does not exist");
  return g;
}

std::string AcDbName(const std::string& t) {
  if (t == "LINE") return "AcDbLine";
  if (t == "CIRCLE") return "AcDbCircle";
  if (t == "ARC") return "AcDbArc";
  if (t == "POINT") return "AcDbPoint";
  if (t == "LWPOLYLINE") return "AcDbPolyline";
  if (t == "SPLINE") return "AcDbSpline";
  if (t == "ELLIPSE") return "AcDbEllipse";
  if (t == "3DSOLID") return "AcDb3dSolid";
  if (t == "SURFACE") return "AcDbSurface";
  if (t == "MESH") return "AcDbSubDMesh";
  return "AcDbEntity";
}
std::string IAcadName(const std::string& t) {
  if (t == "LINE") return "IAcadLine";
  if (t == "CIRCLE") return "IAcadCircle";
  if (t == "ARC") return "IAcadArc";
  if (t == "POINT") return "IAcadPoint";
  if (t == "LWPOLYLINE") return "IAcadLWPolyline";
  if (t == "SPLINE") return "IAcadSpline";
  if (t == "ELLIPSE") return "IAcadEllipse";
  if (t == "3DSOLID") return "IAcad3DSolid";
  if (t == "SURFACE") return "IAcadSurface";
  if (t == "MESH") return "IAcadSubDMesh";
  return "IAcadEntity";
}

[[noreturn]] void ReadOnly(const std::string& prop) { Fail("property " + prop + " is read-only"); }
[[noreturn]] void NoProp(const std::string& type, const std::string& prop) { Fail("the " + IAcadName(type) + " object has no property " + prop); }

std::string Canon(std::string s) { return Lower(std::move(s)); }

Value VlaGetProperty(Interp& in, const Value& obj, const std::string& prop_in) {
  const std::string prop = Canon(prop_in);
  if (obj.s == "app") {
    if (prop == "activedocument") return MakeVla("doc");
    if (prop == "name") return Str("Dino 8");
    Fail("the Application object has no property " + prop_in);
  }
  if (obj.s == "doc") {
    if (prop == "modelspace") return MakeVla("ms");
    if (prop == "application") return MakeVla("app");
    Fail("the Document object has no property " + prop_in);
  }
  if (obj.s == "ms") {
    if (prop == "count") return Int(in.host.all_entities ? static_cast<long long>(in.host.all_entities().size()) : 0);
    if (prop == "name") return Str("*Model_Space");
    Fail("the ModelSpace object has no property " + prop_in);
  }
  const std::vector<DxfItem> g = LoadEnt(in, obj.i);
  const std::string type = GroupStr(g, 0);
  if (prop == "layer") return Str(GroupStr(g, 8));
  if (prop == "color") return Int(GroupOf(g, 62) ? GroupOf(g, 62)->i : 256);
  if (prop == "handle") return Str(GroupStr(g, 5));
  if (prop == "objectname") return Str(AcDbName(type));
  if (prop == "objectid") return Int(obj.i);
  if (type == "LINE") {
    const P3 a = GroupPt(g, 10), b = GroupPt(g, 11);
    const double dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    if (prop == "startpoint") return Pt3Value(a);
    if (prop == "endpoint") return Pt3Value(b);
    if (prop == "length") return Real(std::sqrt(dx * dx + dy * dy + dz * dz));
    if (prop == "angle") { double an = std::atan2(dy, dx); if (an < 0) an += kTwoPi; return Real(an); }
    if (prop == "delta") return Pt3Value({dx, dy, dz});
  } else if (type == "CIRCLE" || type == "ARC") {
    const double r = GroupReal(g, 40);
    if (prop == "center") return Pt3Value(GroupPt(g, 10));
    if (prop == "radius") return Real(r);
    if (prop == "diameter") return Real(2 * r);
    if (prop == "normal") return Pt3Value(GroupPt(g, 210));
    if (type == "CIRCLE") {
      if (prop == "circumference") return Real(kTwoPi / 2 * 2 * r);
      if (prop == "area") return Real(kTwoPi / 2 * r * r);
    } else {
      const double a0 = GroupReal(g, 50), a1 = GroupReal(g, 51), c = GroupPt(g, 10)[0], d = GroupPt(g, 10)[1];
      if (prop == "startangle") return Real(a0);
      if (prop == "endangle") return Real(a1);
      if (prop == "totalangle") return Real(a1 - a0);
      if (prop == "arclength") return Real(r * (a1 - a0));
      if (prop == "area") return Real(r * r * (a1 - a0) / 2);
      if (prop == "startpoint") return Pt3Value({c + r * std::cos(a0), d + r * std::sin(a0), GroupPt(g, 10)[2]});
      if (prop == "endpoint") return Pt3Value({c + r * std::cos(a1), d + r * std::sin(a1), GroupPt(g, 10)[2]});
    }
  } else if (type == "POINT") {
    if (prop == "coordinates") return Pt3Value(GroupPt(g, 10));
  } else if (type == "LWPOLYLINE") {
    const std::vector<P3> v = GroupPts(g, 10);
    const bool closed = GroupOf(g, 70) && GroupOf(g, 70)->i == 1;
    if (prop == "closed") return Bool(closed);
    if (prop == "coordinates") { List l; for (const P3& p : v) { l.push_back(Real(p[0])); l.push_back(Real(p[1])); } return MakeList(l); }
    if (prop == "length") {
      double s = 0;
      for (size_t k = 0; k + 1 < v.size(); ++k) s += std::hypot(v[k + 1][0] - v[k][0], v[k + 1][1] - v[k][1]);
      if (closed && v.size() > 1) s += std::hypot(v.front()[0] - v.back()[0], v.front()[1] - v.back()[1]);
      return Real(s);
    }
    if (prop == "area") {
      double s = 0;
      for (size_t k = 0; k < v.size(); ++k) { const P3& a = v[k]; const P3& b = v[(k + 1) % v.size()]; s += a[0] * b[1] - b[0] * a[1]; }
      return Real(std::fabs(s) / 2);
    }
  } else if (type == "ELLIPSE") {
    const P3 m = GroupPt(g, 11);
    const double major = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]), ratio = GroupReal(g, 40);
    if (prop == "center") return Pt3Value(GroupPt(g, 10));
    if (prop == "majorradius") return Real(major);
    if (prop == "minorradius") return Real(major * ratio);
    if (prop == "radiusratio") return Real(ratio);
    if (prop == "majoraxis") return Pt3Value(m);
    if (prop == "startangle") return Real(GroupReal(g, 41));
    if (prop == "endangle") return Real(GroupReal(g, 42, kTwoPi));
    if (prop == "area") return Real(kTwoPi / 2 * major * major * ratio);
  } else if (type == "SPLINE") {
    const std::vector<P3> cv = GroupPts(g, 10);
    if (prop == "degree") return Int(GroupOf(g, 71) ? GroupOf(g, 71)->i : 0);
    if (prop == "numberofcontrolpoints") return Int(static_cast<long long>(cv.size()));
    if (prop == "controlpoints") { List l; for (const P3& p : cv) for (double c : p) l.push_back(Real(c)); return MakeList(l); }
  }
  NoProp(type, prop_in);
}

// Replaces/sets group `code` in a list of groups.
void SetGroup(std::vector<DxfItem>& g, const DxfItem& d) {
  for (DxfItem& x : g) if (x.code == d.code) { x = d; return; }
  g.push_back(d);
}
DxfItem GPt(int code, P3 p) { DxfItem d; d.code = code; d.k = DxfItem::K::Point; d.x = p[0]; d.y = p[1]; d.z = p[2]; return d; }
DxfItem GReal(int code, double v) { DxfItem d; d.code = code; d.k = DxfItem::K::Real; d.x = v; return d; }
DxfItem GInt(int code, long long v) { DxfItem d; d.code = code; d.k = DxfItem::K::Int; d.i = v; d.x = static_cast<double>(v); return d; }
DxfItem GStr(int code, const std::string& s) { DxfItem d; d.code = code; d.k = DxfItem::K::Str; d.s = s; return d; }

void ApplyMod(Interp& in, long long id, const std::vector<DxfItem>& g, const char* what) {
  if (!in.host.ent_mod) Fail("entity access is not available here");
  const std::string why = in.host.ent_mod(id, g);
  if (!why.empty()) Fail(std::string(what) + " failed: " + why);
}

Value VlaPutProperty(Interp& in, const Value& obj, const std::string& prop_in, const Value& val) {
  const std::string prop = Canon(prop_in);
  if (obj.s != "ent") Fail("the " + std::string(obj.s == "app" ? "Application" : obj.s == "doc" ? "Document" : "ModelSpace") + " object has no writable property " + prop_in);
  std::vector<DxfItem> g = LoadEnt(in, obj.i);
  const std::string type = GroupStr(g, 0);
  auto num = [&](const char* what) { if (!IsNum(val)) Fail(std::string("bad argument type: numberp: ") + Interp::Format(val, true) + " for " + what); return D(val); };
  if (prop == "layer") {
    if (val.t != Value::T::Str) Fail("bad argument type: stringp for Layer");
    SetGroup(g, GStr(8, val.s));
  } else if (prop == "color") {
    if (val.t != Value::T::Int) Fail("bad argument type: integerp for Color");
    SetGroup(g, GInt(62, val.i));
  } else if (prop == "objectname" || prop == "handle" || prop == "objectid" || prop == "length" || prop == "area" || prop == "arclength" ||
             prop == "totalangle" || prop == "circumference" || prop == "angle" || prop == "delta" || prop == "normal") {
    ReadOnly(prop_in);
  } else if (type == "LINE" && (prop == "startpoint" || prop == "endpoint")) {
    SetGroup(g, GPt(prop == "startpoint" ? 10 : 11, NeedP3("Put", val)));
  } else if ((type == "CIRCLE" || type == "ARC") && prop == "center") {
    SetGroup(g, GPt(10, NeedP3("Put", val)));
  } else if ((type == "CIRCLE" || type == "ARC") && (prop == "radius" || prop == "diameter")) {
    SetGroup(g, GReal(40, prop == "radius" ? num("Radius") : num("Diameter") / 2));
  } else if (type == "ARC" && (prop == "startangle" || prop == "endangle")) {
    SetGroup(g, GReal(prop == "startangle" ? 50 : 51, num("angle")));
  } else if (type == "ARC" && (prop == "startpoint" || prop == "endpoint")) {
    ReadOnly(prop_in);
  } else if (type == "POINT" && prop == "coordinates") {
    SetGroup(g, GPt(10, NeedP3("Put", val)));
  } else if (type == "LWPOLYLINE" && prop == "closed") {
    SetGroup(g, GInt(70, Truthy(val) ? 1 : 0));
  } else if (type == "LWPOLYLINE" && prop == "coordinates") {
    const List& c = NeedList("Put", val);
    if (c.size() < 6 || c.size() % 2 != 0) Fail("Coordinates needs a flat list x y x y ... with at least three vertices");
    double z = 0;
    const std::vector<P3> old = GroupPts(g, 10);
    if (!old.empty()) z = old.front()[2];
    std::vector<DxfItem> out;
    for (const DxfItem& d : g) if (d.code != 10 && d.code != 90) out.push_back(d);
    out.push_back(GInt(90, static_cast<long long>(c.size() / 2)));
    for (size_t k = 0; k < c.size(); k += 2) {
      if (!IsNum(c[k]) || !IsNum(c[k + 1])) Fail("bad argument type: coordinates must be numbers");
      out.push_back(GPt(10, {D(c[k]), D(c[k + 1]), z}));
    }
    g = out;
  } else if (type == "ELLIPSE" || type == "SPLINE") {
    Fail("the " + IAcadName(type) + " object's " + prop_in + " cannot be changed (only Layer and Color)");
  } else {
    NoProp(type, prop_in);
  }
  ApplyMod(in, obj.i, g, "put-property");
  return Nil();
}

// Moves / rotates / scales every point of the entity with `fn`; `angle_shift` adds to an arc's angles, `radius_scale` multiplies radii.
void TransformEnt(std::vector<DxfItem>& g, const std::string& type, const std::function<P3(P3)>& fn, double angle_shift, double radius_scale) {
  for (DxfItem& d : g) {
    if (d.k != DxfItem::K::Point) continue;
    if (d.code == 10 || (d.code == 11 && type == "LINE")) { const P3 p = fn({d.x, d.y, d.z}); d.x = p[0]; d.y = p[1]; d.z = p[2]; }
  }
  for (DxfItem& d : g) {
    if (d.code == 40 && (type == "CIRCLE" || type == "ARC")) d.x *= radius_scale;
    if (type == "ARC" && (d.code == 50 || d.code == 51)) d.x += angle_shift;
  }
}

Value NewestVla(Interp& in) {
  std::optional<long long> id = in.host.last_entity ? in.host.last_entity() : std::nullopt;
  if (!id) Fail("nothing was created");
  return MakeVla("ent", *id);
}
Value MakeInSpace(Interp& in, const std::vector<DxfItem>& items) {
  if (!in.host.ent_make) Fail("entity access is not available here");
  const std::string why = in.host.ent_make(items);
  if (!why.empty()) Fail("method failed: " + why);
  return NewestVla(in);
}

Value VlaInvoke(Interp& in, const Value& obj, const std::string& method_in, const List& a) {
  const std::string m = Canon(method_in);
  auto need = [&](size_t n) { if (a.size() != n) Fail("wrong number of arguments for method " + method_in + " (" + std::to_string(n) + " needed)"); };
  if (obj.s == "ms") {
    if (m == "item") {
      need(1);
      if (a[0].t != Value::T::Int || a[0].i < 0) Fail("invalid index");
      const std::vector<long long> ids = in.host.all_entities ? in.host.all_entities() : std::vector<long long>();
      if (static_cast<size_t>(a[0].i) >= ids.size()) Fail("invalid index");
      return MakeVla("ent", ids[static_cast<size_t>(a[0].i)]);
    }
    if (m == "addline") { need(2); return MakeInSpace(in, {GStr(0, "LINE"), GPt(10, NeedP3("AddLine", a[0])), GPt(11, NeedP3("AddLine", a[1]))}); }
    if (m == "addcircle") {
      need(2);
      return MakeInSpace(in, {GStr(0, "CIRCLE"), GPt(10, NeedP3("AddCircle", a[0])), GReal(40, NeedNum("AddCircle", a[1]))});
    }
    if (m == "addarc") {
      need(4);
      return MakeInSpace(in, {GStr(0, "ARC"), GPt(10, NeedP3("AddArc", a[0])), GReal(40, NeedNum("AddArc", a[1])), GReal(50, NeedNum("AddArc", a[2])), GReal(51, NeedNum("AddArc", a[3]))});
    }
    if (m == "addpoint") { need(1); return MakeInSpace(in, {GStr(0, "POINT"), GPt(10, NeedP3("AddPoint", a[0]))}); }
    if (m == "addlightweightpolyline") {
      need(1);
      const List& c = NeedList("AddLightWeightPolyline", a[0]);
      if (c.size() < 6 || c.size() % 2 != 0) Fail("AddLightWeightPolyline needs a flat list x y x y ... with at least three vertices");
      std::vector<DxfItem> it{GStr(0, "LWPOLYLINE"), GInt(90, static_cast<long long>(c.size() / 2))};
      for (size_t k = 0; k < c.size(); k += 2) {
        if (!IsNum(c[k]) || !IsNum(c[k + 1])) Fail("bad argument type: coordinates must be numbers");
        it.push_back(GPt(10, {D(c[k]), D(c[k + 1]), 0}));
      }
      return MakeInSpace(in, it);
    }
    if (m == "addellipse") {
      need(3);
      return MakeInSpace(in, {GStr(0, "ELLIPSE"), GPt(10, NeedP3("AddEllipse", a[0])), GPt(11, NeedP3("AddEllipse", a[1])), GReal(40, NeedNum("AddEllipse", a[2]))});
    }
    Fail("the ModelSpace object has no method " + method_in);
  }
  if (obj.s != "ent") Fail("the " + std::string(obj.s == "app" ? "Application" : "Document") + " object has no method " + method_in);
  if (m == "update") { if (in.host.ent_upd) in.host.ent_upd(obj.i); return Nil(); }
  if (m == "delete" || m == "erase") {
    LoadEnt(in, obj.i);
    if (!in.host.ent_del || !in.host.ent_del(obj.i)) Fail("the object could not be deleted");
    return Nil();
  }
  std::vector<DxfItem> g = LoadEnt(in, obj.i);
  const std::string type = GroupStr(g, 0);
  const bool movable = type == "LINE" || type == "CIRCLE" || type == "ARC" || type == "POINT" || type == "LWPOLYLINE";
  if (m == "copy") {
    need(0);
    std::vector<DxfItem> mk;
    for (const DxfItem& d : g) if (d.code != -1 && d.code != 5) mk.push_back(d);
    return MakeInSpace(in, mk);
  }
  if (m == "move" || m == "rotate" || m == "scaleentity") {
    if (!movable) Fail("the method " + method_in + " is not supported for " + IAcadName(type));
    if (m == "move") {
      need(2);
      const P3 f = NeedP3("Move", a[0]), t = NeedP3("Move", a[1]);
      TransformEnt(g, type, [&](P3 p) { return P3{p[0] + t[0] - f[0], p[1] + t[1] - f[1], p[2] + t[2] - f[2]}; }, 0, 1);
    } else if (m == "rotate") {
      need(2);
      const P3 b = NeedP3("Rotate", a[0]);
      const double ang = NeedNum("Rotate", a[1]), c = std::cos(ang), s = std::sin(ang);
      TransformEnt(g, type, [&](P3 p) { const double x = p[0] - b[0], y = p[1] - b[1]; return P3{b[0] + c * x - s * y, b[1] + s * x + c * y, p[2]}; }, ang, 1);
    } else {
      need(2);
      const P3 b = NeedP3("ScaleEntity", a[0]);
      const double f = NeedNum("ScaleEntity", a[1]);
      if (!(f > 0)) Fail("ScaleEntity needs a positive scale factor");
      TransformEnt(g, type, [&](P3 p) { return P3{b[0] + f * (p[0] - b[0]), b[1] + f * (p[1] - b[1]), b[2] + f * (p[2] - b[2])}; }, 0, f);
    }
    ApplyMod(in, obj.i, g, method_in.c_str());
    return Nil();
  }
  Fail("the " + IAcadName(type) + " object has no method " + method_in);
}

// vla-get-<prop>, vla-put-<prop>, vla-<method>
Value VlaDispatch(Interp& in, const std::string& name, const List& args) {
  if (args.empty() || args[0].t != Value::T::Vla) Fail("bad argument type: VLA-OBJECT in " + Upper(name));
  if (name.rfind("vla-get-", 0) == 0) { if (args.size() != 1) Fail("too many arguments to " + Upper(name)); return VlaGetProperty(in, args[0], name.substr(8)); }
  if (name.rfind("vla-put-", 0) == 0) { if (args.size() != 2) Fail(std::string(args.size() < 2 ? "too few" : "too many") + " arguments to " + Upper(name)); return VlaPutProperty(in, args[0], name.substr(8), args[1]); }
  return VlaInvoke(in, args[0], name.substr(4), List(args.begin() + 1, args.end()));
}

// vlax-curve-*: the entity or object, an operation, numbers in and out.
std::vector<double> CurveOp(Interp& in, const char* fn, const Value& e, const char* op, const std::vector<double>& arg) {
  const long long id = NeedEntId(fn, e);
  if (!in.host.curve_op) Fail("curve access is not available here");
  std::vector<double> out;
  const std::string why = in.host.curve_op(id, op, arg, out);
  if (!why.empty()) Fail(std::string(fn) + ": " + why);
  return out;
}

using BuiltinFn = std::function<Value(Interp&, List&)>;

// ===== ROW2SCR: number / angle / distance conversion, units, wildcards, symbols, file names ===========================
// Everything below follows the AutoLISP reference pages named in the Parity Register rows D8-AUTOCAD-LISP-0112 (angtof),
// -0113 (angtos), -0152 (distof), -0141 (cvunit), -0266 (rtos), -0267 (set), -0277 (snvalid), -0309 / -0310 (vl-bb-ref /
// vl-bb-set), -0330 to -0332 (vl-filename-*), -0334 (vl-list*), -0367 / -0368 (vl-symbol-value / vl-symbolp), -0374
// (wcmatch) and -0125 (atoms-family).
bool IsBuiltinName(const std::string& n);              // defined after struct Builtins
std::vector<std::string> BuiltinNames();               // likewise

constexpr double kRw2Pi = 3.14159265358979323846;

// A numeric system variable (AUNITS, AUPREC, LUNITS, LUPREC, UNITMODE) when the host has it, else the AutoCAD default.
double HostNum(Interp& in, const char* name, double def) {
  if (!in.host.getvar) return def;
  DxfItem d;
  if (!in.host.getvar(name, d)) return def;
  if (d.k == DxfItem::K::Int) return static_cast<double>(d.i);
  if (d.k == DxfItem::K::Real) return d.x;
  return def;
}
long long NeedWhole(const char* fn, const Value& v) {
  if (v.t != Value::T::Int) Fail(std::string("bad argument type: integerp: ") + Interp::Format(v, true) + " in " + fn);
  return v.i;
}

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

long long Gcd64(long long a, long long b) { while (b) { const long long t = a % b; a = b; b = t; } return a; }

// ---- rtos ------------------------------------------------------------------------------------------------------
// mode 1 scientific, 2 decimal, 3 engineering (feet and decimal inches), 4 architectural (feet, inches and a fraction
// of an inch with denominator 2^precision), 5 fractional (units and a fraction with denominator 2^precision). The
// drawing unit of modes 3 to 5 is the inch.
std::string RtosFormat(double x, int mode, int prec) {
  char buf[96];
  if (mode == 1) { std::snprintf(buf, sizeof buf, "%.*E", prec, x); return buf; }
  if (mode == 2) { std::snprintf(buf, sizeof buf, "%.*f", prec, x); return buf; }
  const bool neg = x < 0;
  const double ax = std::fabs(x);
  if (mode == 3) {
    double scale = 1;
    for (int k = 0; k < prec; ++k) scale *= 10;
    if (ax * scale > 9e15) Fail("bad argument value: the number is too large for engineering units at this precision");
    const long long units = std::llround(ax * scale);
    const long long per_foot = 12 * static_cast<long long>(scale);
    const long long feet = units / per_foot, rem = units % per_foot;
    const long long whole = rem / static_cast<long long>(scale), frac = rem % static_cast<long long>(scale);
    std::string s = (neg && units != 0 ? "-" : "") + std::to_string(feet) + "'-" + std::to_string(whole);
    if (prec > 0) { std::string f = std::to_string(frac); s += "." + std::string(static_cast<size_t>(prec) - std::min(static_cast<size_t>(prec), f.size()), '0') + f; }
    return s + "\"";
  }
  if (prec > 8) Fail("bad argument value: precision of at most 8 for fractional units");
  const long long den = 1LL << prec;
  if (ax * static_cast<double>(den) > 9e15) Fail("bad argument value: the number is too large for these units");
  const long long units = std::llround(ax * static_cast<double>(den));
  long long whole = units / den, num = units % den, d = den;
  if (num) { const long long g = Gcd64(num, d); num /= g; d /= g; }
  const std::string sign = neg && units != 0 ? "-" : "";
  std::string frac = num ? std::to_string(num) + "/" + std::to_string(d) : "";
  if (mode == 5) {
    if (!num) return sign + std::to_string(whole);
    return sign + (whole ? std::to_string(whole) + " " : "") + frac;
  }
  return sign + std::to_string(whole / 12) + "'-" + std::to_string(whole % 12) + (num ? " " + frac : "") + "\"";
}

// ---- distof ----------------------------------------------------------------------------------------------------
bool ParsePlain(const std::string& s, bool allow_exp, double& out) {
  if (s.empty()) return false;
  size_t digits = 0;
  for (char c : s) {
    if (std::isdigit(static_cast<unsigned char>(c))) ++digits;
    else if (c == '+' || c == '-' || c == '.') continue;
    else if ((c == 'e' || c == 'E') && allow_exp) continue;
    else return false;
  }
  if (!digits) return false;
  char* end = nullptr;
  out = std::strtod(s.c_str(), &end);
  return end && *end == 0;
}
bool ParseWholeDigits(const std::string& s, long long& out) {
  if (s.empty() || s.size() > 15) return false;
  for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  out = std::atoll(s.c_str());
  return true;
}
// "17", "17.5", "1/2", "17 1/2" (the inches or the units part, no sign).
bool ParseMixed(const std::string& in_s, bool allow_decimal, double& out) {
  const std::string s = Trim(in_s);
  if (s.empty()) return false;
  const size_t sp = s.find_first_of(" \t");
  auto frac = [](const std::string& t, double& v) {
    const size_t sl = t.find('/');
    if (sl == std::string::npos) return false;
    long long n, d;
    if (!ParseWholeDigits(t.substr(0, sl), n) || !ParseWholeDigits(t.substr(sl + 1), d) || d == 0) return false;
    v = static_cast<double>(n) / static_cast<double>(d);
    return true;
  };
  if (sp == std::string::npos) {
    if (s.find('/') != std::string::npos) return frac(s, out);
    if (s.find_first_of("+-") != std::string::npos) return false;
    return allow_decimal && ParsePlain(s, false, out);
  }
  long long w;
  double f;
  if (!ParseWholeDigits(s.substr(0, sp), w) || !frac(Trim(s.substr(sp + 1)), f)) return false;
  out = static_cast<double>(w) + f;
  return true;
}
// mode 1..5 as rtos writes them (3 and 4 are interchangeable); false: not in that format.
bool DistOf(const std::string& in_s, int mode, double& out) {
  std::string s = Trim(in_s);
  if (s.empty()) return false;
  if (mode == 1) return ParsePlain(s, true, out);
  if (mode == 2) return ParsePlain(s, false, out);
  bool neg = false;
  if (s[0] == '-') { neg = true; s.erase(0, 1); s = Trim(s); }
  else if (s[0] == '+') { s.erase(0, 1); s = Trim(s); }
  if (s.empty()) return false;
  double v = 0;
  if (mode == 5) {
    if (!ParseMixed(s, true, v)) return false;
  } else {
    double feet = 0;
    const size_t q = s.find('\'');
    std::string rest = s;
    if (q != std::string::npos) {
      if (!ParsePlain(Trim(s.substr(0, q)), false, feet) || feet < 0) return false;
      rest = Trim(s.substr(q + 1));
      if (!rest.empty() && rest[0] == '-') rest = Trim(rest.substr(1));
    }
    double inches = 0;
    if (!rest.empty() && rest.back() == '"') rest = Trim(rest.substr(0, rest.size() - 1));
    if (!rest.empty()) { if (!ParseMixed(rest, true, inches)) return false; }
    else if (q == std::string::npos) return false;
    v = feet * 12 + inches;
  }
  out = neg ? -v : v;
  return true;
}

// ---- angtos / angtof -------------------------------------------------------------------------------------------
double NormAngle(double a) {
  a = std::fmod(a, 2 * kRw2Pi);
  if (a < 0) a += 2 * kRw2Pi;
  return a;
}
// Non-negative degrees as degrees, minutes and seconds: precision 0 whole degrees, 1-2 minutes, 3-4 seconds, 5 and
// more decimals of a second (precision - 4 of them).
std::string DmsFormat(double deg, int prec) {
  long long per_deg;
  int sec_dec = 0;
  if (prec <= 0) per_deg = 1;
  else if (prec <= 2) per_deg = 60;
  else if (prec <= 4) per_deg = 3600;
  else { sec_dec = std::min(prec - 4, 6); per_deg = 3600; for (int k = 0; k < sec_dec; ++k) per_deg *= 10; }
  long long n = std::llround(deg * static_cast<double>(per_deg));
  char buf[64];
  if (prec <= 0) return std::to_string(n) + "d";
  if (prec <= 2) return std::to_string(n / 60) + "d" + std::to_string(n % 60) + "'";
  const long long deg_i = n / (per_deg), rem = n % per_deg;
  long long unit = per_deg / 60;                       // one minute, in the lowest unit
  const long long minutes = rem / unit, sec_units = rem % unit;
  std::string s = std::to_string(deg_i) + "d" + std::to_string(minutes) + "'";
  if (sec_dec == 0) return s + std::to_string(sec_units) + "\"";
  long long sd = 1;
  for (int k = 0; k < sec_dec; ++k) sd *= 10;
  std::snprintf(buf, sizeof buf, "%lld.%0*lld", sec_units / sd, sec_dec, sec_units % sd);
  return s + buf + "\"";
}
std::string AngTos(double rad, int mode, int prec, bool compact) {
  const double a = NormAngle(rad);
  const double deg = a * 180.0 / kRw2Pi;
  char buf[96];
  if (mode == 0 || mode == 2 || mode == 3) {
    const double v = mode == 0 ? deg : mode == 2 ? a * 200.0 / kRw2Pi : a;
    const double full = mode == 0 ? 360.0 : mode == 2 ? 400.0 : 2 * kRw2Pi;
    std::snprintf(buf, sizeof buf, "%.*f", prec, v);
    // a value that rounds up to a full turn is the start of the next one
    if (mode != 3 && std::atof(buf) >= full) std::snprintf(buf, sizeof buf, "%.*f", prec, 0.0);
    return std::string(buf) + (mode == 2 ? "g" : mode == 3 ? "r" : "");
  }
  if (mode == 1) {
    std::string s = DmsFormat(deg, prec);
    if (s.rfind("360d", 0) == 0) s = DmsFormat(0, prec);   // a rounded-up full turn is the start of the next
    return s;
  }
  // surveyor's units: a bearing from north or south toward east or west
  const double eps = 1e-9;
  const char* sp = compact ? "" : " ";
  auto cardinal = [&](const char* c) { return std::string(c); };
  if (deg < eps || deg > 360.0 - eps) return cardinal("E");
  if (std::fabs(deg - 90.0) < eps) return cardinal("N");
  if (std::fabs(deg - 180.0) < eps) return cardinal("W");
  if (std::fabs(deg - 270.0) < eps) return cardinal("S");
  char ns, ew;
  double th;
  if (deg < 90) { ns = 'N'; ew = 'E'; th = 90 - deg; }
  else if (deg < 180) { ns = 'N'; ew = 'W'; th = deg - 90; }
  else if (deg < 270) { ns = 'S'; ew = 'W'; th = 270 - deg; }
  else { ns = 'S'; ew = 'E'; th = deg - 270; }
  return std::string(1, ns) + sp + DmsFormat(th, prec) + sp + std::string(1, ew);
}

// "30d30'15\"", "30d30'", "30d", "30.5" (degrees); false when it is not that.
bool ParseDms(const std::string& s, double& deg) {
  size_t i = 0;
  bool neg = false;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; ++i; }
  auto number = [&](double& v) {
    const size_t b = i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
    if (i == b) return false;
    return ParsePlain(s.substr(b, i - b), false, v);
  };
  double d = 0, m = 0, sec = 0;
  if (!number(d)) return false;
  if (i == s.size()) { deg = neg ? -d : d; return true; }
  if (s[i] != 'd' && s[i] != 'D') return false;
  ++i;
  if (i < s.size()) {
    if (!number(m) || i >= s.size() || s[i] != '\'') return false;
    ++i;
    if (i < s.size()) {
      if (!number(sec) || i >= s.size() || s[i] != '"') return false;
      ++i;
      if (i != s.size()) return false;
    }
  }
  const double v = d + m / 60.0 + sec / 3600.0;
  deg = neg ? -v : v;
  return true;
}
bool AngToF(const std::string& in_s, int mode, double& rad) {
  std::string s = Trim(in_s);
  if (s.empty()) return false;
  double v = 0;
  if (mode == 0 || mode == 2 || mode == 3) {
    const char suffix = mode == 0 ? 'd' : mode == 2 ? 'g' : 'r';
    if (std::tolower(static_cast<unsigned char>(s.back())) == suffix) s.pop_back();
    if (!ParsePlain(Trim(s), false, v)) return false;
    rad = mode == 0 ? v * kRw2Pi / 180.0 : mode == 2 ? v * kRw2Pi / 200.0 : v;
  } else if (mode == 1) {
    if (!ParseDms(s, v)) return false;
    rad = v * kRw2Pi / 180.0;
  } else {
    s = Upper(s);
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
    if (s == "E") rad = 0;
    else if (s == "N") rad = kRw2Pi / 2;
    else if (s == "W") rad = kRw2Pi;
    else if (s == "S") rad = 3 * kRw2Pi / 2;
    else {
      if (s.size() < 3 || (s.front() != 'N' && s.front() != 'S') || (s.back() != 'E' && s.back() != 'W')) return false;
      double th;
      if (!ParseDms(Lower(s.substr(1, s.size() - 2)), th) || th < 0 || th > 90) return false;
      const bool north = s.front() == 'N', east = s.back() == 'E';
      const double deg = north ? (east ? 90 - th : 90 + th) : (east ? 270 + th : 270 - th);
      rad = deg * kRw2Pi / 180.0;
    }
  }
  rad = NormAngle(rad);
  return true;
}

// ---- cvunit ----------------------------------------------------------------------------------------------------
// Dimensions: length, mass, time, temperature, angle. Factors to the SI unit (meter, kilogram, second, kelvin, radian).
struct UnitDef { const char* names; double f; int d[5]; double off; };
const UnitDef kUnits[] = {
  {"meter|meters|metre|metres|m", 1, {1, 0, 0, 0, 0}, 0},
  {"kilometer|kilometers|km", 1000, {1, 0, 0, 0, 0}, 0},
  {"centimeter|centimeters|cm", 0.01, {1, 0, 0, 0, 0}, 0},
  {"millimeter|millimeters|mm", 0.001, {1, 0, 0, 0, 0}, 0},
  {"micrometer|micrometers|micron|microns", 1e-6, {1, 0, 0, 0, 0}, 0},
  {"inch|inches|in", 0.0254, {1, 0, 0, 0, 0}, 0},
  {"mil|thou", 0.0000254, {1, 0, 0, 0, 0}, 0},
  {"foot|feet|ft", 0.3048, {1, 0, 0, 0, 0}, 0},
  {"yard|yards|yd", 0.9144, {1, 0, 0, 0, 0}, 0},
  {"mile|miles|mi", 1609.344, {1, 0, 0, 0, 0}, 0},
  {"furlong|furlongs", 201.168, {1, 0, 0, 0, 0}, 0},
  {"acre|acres", 4046.8564224, {2, 0, 0, 0, 0}, 0},
  {"hectare|hectares|ha", 10000, {2, 0, 0, 0, 0}, 0},
  {"liter|liters|litre|litres|l", 0.001, {3, 0, 0, 0, 0}, 0},
  {"milliliter|milliliters|ml", 1e-6, {3, 0, 0, 0, 0}, 0},
  {"gallon|gallons|gal", 0.003785411784, {3, 0, 0, 0, 0}, 0},
  {"quart|quarts|qt", 0.000946352946, {3, 0, 0, 0, 0}, 0},
  {"pint|pints|pt", 0.000473176473, {3, 0, 0, 0, 0}, 0},
  {"cup|cups", 0.0002365882365, {3, 0, 0, 0, 0}, 0},
  {"second|seconds|sec|s", 1, {0, 0, 1, 0, 0}, 0},
  {"minute|minutes|min", 60, {0, 0, 1, 0, 0}, 0},
  {"hour|hours|hr|h", 3600, {0, 0, 1, 0, 0}, 0},
  {"day|days", 86400, {0, 0, 1, 0, 0}, 0},
  {"week|weeks", 604800, {0, 0, 1, 0, 0}, 0},
  {"kilogram|kilograms|kg", 1, {0, 1, 0, 0, 0}, 0},
  {"gram|grams|g", 0.001, {0, 1, 0, 0, 0}, 0},
  {"milligram|milligrams|mg", 1e-6, {0, 1, 0, 0, 0}, 0},
  {"tonne|tonnes", 1000, {0, 1, 0, 0, 0}, 0},
  {"pound|pounds|lb|lbs", 0.45359237, {0, 1, 0, 0, 0}, 0},
  {"ounce|ounces|oz", 0.028349523125, {0, 1, 0, 0, 0}, 0},
  {"radian|radians|rad", 1, {0, 0, 0, 0, 1}, 0},
  {"degree|degrees|deg", kRw2Pi / 180.0, {0, 0, 0, 0, 1}, 0},
  {"grad|grads|gradian|gradians", kRw2Pi / 200.0, {0, 0, 0, 0, 1}, 0},
  {"circle|circles|revolution|revolutions", 2 * kRw2Pi, {0, 0, 0, 0, 1}, 0},
  {"arcminute|arcminutes|arcmin", kRw2Pi / 10800.0, {0, 0, 0, 0, 1}, 0},
  {"arcsecond|arcseconds|arcsec", kRw2Pi / 648000.0, {0, 0, 0, 0, 1}, 0},
  {"kelvin|k", 1, {0, 0, 0, 1, 0}, 0},
  {"celsius|c|degc", 1, {0, 0, 0, 1, 0}, 273.15},
  {"fahrenheit|f|degf", 5.0 / 9.0, {0, 0, 0, 1, 0}, 459.67 * 5.0 / 9.0},
  {"rankin|rankine|r", 5.0 / 9.0, {0, 0, 0, 1, 0}, 0},
  {"newton|newtons|n", 1, {1, 1, -2, 0, 0}, 0},
  {"kilonewton|kilonewtons|kn", 1000, {1, 1, -2, 0, 0}, 0},
  {"lbf", 4.4482216152605, {1, 1, -2, 0, 0}, 0},
  {"pascal|pascals|pa", 1, {-1, 1, -2, 0, 0}, 0},
  {"kilopascal|kilopascals|kpa", 1000, {-1, 1, -2, 0, 0}, 0},
  {"bar", 100000, {-1, 1, -2, 0, 0}, 0},
  {"psi", 6894.757293168, {-1, 1, -2, 0, 0}, 0},
  {"joule|joules|j", 1, {2, 1, -2, 0, 0}, 0},
  {"kilojoule|kilojoules|kj", 1000, {2, 1, -2, 0, 0}, 0},
  {"watt|watts|w", 1, {2, 1, -3, 0, 0}, 0},
  {"kilowatt|kilowatts|kw", 1000, {2, 1, -3, 0, 0}, 0},
  {"horsepower|hp", 745.69987158227, {2, 1, -3, 0, 0}, 0},
};

struct UnitExpr { double f = 1; int d[5] = {0, 0, 0, 0, 0}; double off = 0; };

bool UnitName(const std::string& name, const UnitDef*& out) {
  for (const UnitDef& u : kUnits) {
    std::string all = std::string("|") + u.names + "|";
    if (all.find("|" + name + "|") != std::string::npos) { out = &u; return true; }
  }
  return false;
}
// "inch", "sq ft", "cu in", "ft^2", "m/s", "kg*m/s^2"
bool ParseUnitExpr(const std::string& text, UnitExpr& out) {
  const std::string s = Lower(Trim(text));
  if (s.empty()) return false;
  UnitExpr r;
  size_t i = 0;
  int sign = 1, terms = 0;
  for (;;) {
    size_t j = i;
    while (j < s.size() && s[j] != '*' && s[j] != '/') ++j;
    std::string term = Trim(s.substr(i, j - i));
    int power = 1;
    const size_t caret = term.find('^');
    if (caret != std::string::npos) {
      const std::string e = Trim(term.substr(caret + 1));
      char* end = nullptr;
      const long p = std::strtol(e.c_str(), &end, 10);
      if (e.empty() || *end != 0 || p < -6 || p > 6) return false;
      power = static_cast<int>(p);
      term = Trim(term.substr(0, caret));
    }
    if (term.rfind("sq ", 0) == 0) { power *= 2; term = Trim(term.substr(3)); }
    else if (term.rfind("cu ", 0) == 0) { power *= 3; term = Trim(term.substr(3)); }
    const UnitDef* u = nullptr;
    if (!UnitName(term, u)) return false;
    ++terms;
    const int p = sign * power;
    r.f *= std::pow(u->f, p);
    for (int k = 0; k < 5; ++k) r.d[k] += u->d[k] * p;
    if (terms == 1 && p == 1) r.off = u->off;
    if (j >= s.size()) break;
    sign = s[j] == '/' ? -1 : 1;
    i = j + 1;
  }
  if (terms != 1 || r.d[3] == 0) r.off = 0;
  out = r;
  return true;
}
bool CvUnit(double v, const UnitExpr& from, const UnitExpr& to, double& out) {
  for (int k = 0; k < 5; ++k) if (from.d[k] != to.d[k]) return false;
  out = ((v * from.f + from.off) - to.off) / to.f;
  return true;
}

// ---- wcmatch ---------------------------------------------------------------------------------------------------
bool WcOne(const std::string& p, size_t pi, const std::string& s, size_t si) {
  while (pi < p.size()) {
    const char c = p[pi];
    if (c == '*') {
      while (pi < p.size() && p[pi] == '*') ++pi;
      if (pi == p.size()) return true;
      for (size_t k = si; k <= s.size(); ++k) if (WcOne(p, pi, s, k)) return true;
      return false;
    }
    if (si >= s.size()) return false;
    const unsigned char ch = static_cast<unsigned char>(s[si]);
    if (c == '`') {
      ++pi;
      if (pi >= p.size() || p[pi] != s[si]) return false;
    } else if (c == '#') {
      if (!std::isdigit(ch)) return false;
    } else if (c == '@') {
      if (!std::isalpha(ch)) return false;
    } else if (c == '.') {
      if (std::isalnum(ch)) return false;
    } else if (c == '?') {
    } else if (c == '[' && p.find(']', pi + 1) != std::string::npos) {
      const size_t close = p.find(']', pi + 1);
      size_t k = pi + 1;
      bool neg = false;
      if (k < close && p[k] == '~') { neg = true; ++k; }
      bool hit = false;
      for (; k < close; ++k) {
        if (k + 2 < close && p[k + 1] == '-') {
          if (ch >= static_cast<unsigned char>(p[k]) && ch <= static_cast<unsigned char>(p[k + 2])) hit = true;
          k += 2;
        } else if (static_cast<unsigned char>(p[k]) == ch) hit = true;
      }
      if (hit == neg) return false;
      pi = close;
    } else if (p[pi] != s[si]) {
      return false;
    }
    ++pi; ++si;
  }
  return si == s.size();
}
bool WcMatch(std::string s, std::string pattern) {
  if (s.size() > 500) s.resize(500);
  if (pattern.size() > 500) pattern.resize(500);
  size_t from = 0;
  for (;;) {
    size_t c = from;
    bool in_set = false;
    for (; c < pattern.size(); ++c) {
      if (pattern[c] == '`') { ++c; continue; }
      if (pattern[c] == '[') in_set = true;
      else if (pattern[c] == ']') in_set = false;
      else if (pattern[c] == ',' && !in_set) break;
    }
    std::string alt = pattern.substr(from, std::min(c, pattern.size()) - from);
    bool neg = false;
    if (!alt.empty() && alt[0] == '~') { neg = true; alt.erase(0, 1); }
    if (WcOne(alt, 0, s, 0) != neg) return true;
    if (c >= pattern.size()) return false;
    from = c + 1;
  }
}

// ---- snvalid -----------------------------------------------------------------------------------------------------
bool SnValid(const std::string& s, bool bar_ok) {
  if (s.empty() || s.size() > 255) return false;
  static const std::string bad = "<>/\\\":;?*,=`";
  for (size_t k = 0; k < s.size(); ++k) {
    const unsigned char c = static_cast<unsigned char>(s[k]);
    if (c < 32 || c == 127) return false;
    if (bad.find(s[k]) != std::string::npos) return false;
    if (s[k] == '|' && !(bar_ok && k != 0 && k + 1 != s.size())) return false;
  }
  return true;
}

// ---- file names -----------------------------------------------------------------------------------------------
size_t LastSep(const std::string& s) { return s.find_last_of("/\\"); }
std::string FileCase(std::string s) {
#ifdef _WIN32
  return Upper(std::move(s));    // the reference pages: upper case on Windows, as typed on Mac OS and the web
#else
  return s;
#endif
}
std::map<std::string, Value>& Blackboard() { static std::map<std::string, Value> bb; return bb; }

const std::string& NeedSymName(const char* fn, const Value& v) {
  if (v.t != Value::T::Sym) Fail(std::string("bad argument type: symbolp: ") + Interp::Format(v, true) + " in " + fn);
  return v.s;
}

void AddRw2(std::map<std::string, BuiltinFn>& t) {
  // (angtos angle [mode [precision]]): mode 0 degrees, 1 degrees / minutes / seconds, 2 grads, 3 radians, 4 surveyor's units.
  t["angtos"] = [](Interp& in, List& a) {
    NeedArgs("angtos", a, 1, 3);
    const double ang = NeedNum("angtos", a[0]);
    const long long mode = a.size() >= 2 && !IsNil(a[1]) ? NeedWhole("angtos", a[1]) : static_cast<long long>(HostNum(in, "AUNITS", 0));
    const long long prec = a.size() == 3 && !IsNil(a[2]) ? NeedWhole("angtos", a[2]) : static_cast<long long>(HostNum(in, "AUPREC", 0));
    if (mode < 0 || mode > 4) Fail("bad argument value: angtos mode must be 0 to 4");
    if (prec < 0 || prec > 8) Fail("bad argument value: angtos precision must be 0 to 8");
    return Str(AngTos(ang, static_cast<int>(mode), static_cast<int>(prec), HostNum(in, "UNITMODE", 0) != 0));
  };
  // (angtof string [mode]): the angle in radians (0 to 2 pi), nil when the string is not in that format.
  t["angtof"] = [](Interp& in, List& a) {
    NeedArgs("angtof", a, 1, 2);
    const std::string& s = NeedStr("angtof", a[0]);
    const long long mode = a.size() == 2 && !IsNil(a[1]) ? NeedWhole("angtof", a[1]) : static_cast<long long>(HostNum(in, "AUNITS", 0));
    if (mode < 0 || mode > 4) Fail("bad argument value: angtof mode must be 0 to 4");
    double r;
    return AngToF(s, static_cast<int>(mode), r) ? Real(r) : Nil();
  };
  // (distof string [mode]): mode 1 scientific, 2 decimal, 3 engineering, 4 architectural, 5 fractional.
  t["distof"] = [](Interp& in, List& a) {
    NeedArgs("distof", a, 1, 2);
    const std::string& s = NeedStr("distof", a[0]);
    const long long mode = a.size() == 2 && !IsNil(a[1]) ? NeedWhole("distof", a[1]) : static_cast<long long>(HostNum(in, "LUNITS", 2));
    if (mode < 1 || mode > 5) Fail("bad argument value: distof mode must be 1 to 5");
    double r;
    return DistOf(s, static_cast<int>(mode), r) ? Real(r) : Nil();
  };
  // (rtos number [mode [precision]]): every unit mode (replaces the decimal-only version).
  t["rtos"] = [](Interp& in, List& a) {
    NeedArgs("rtos", a, 1, 3);
    const double x = NeedNum("rtos", a[0]);
    const long long mode = a.size() >= 2 && !IsNil(a[1]) ? NeedWhole("rtos", a[1]) : static_cast<long long>(HostNum(in, "LUNITS", 2));
    const long long prec = a.size() == 3 && !IsNil(a[2]) ? NeedWhole("rtos", a[2]) : static_cast<long long>(HostNum(in, "LUPREC", 4));
    if (mode < 1 || mode > 5) Fail("bad argument value: rtos mode must be 1 to 5");
    if (prec < 0 || prec > 16) Fail("bad argument value: precision in rtos must be 0 to 16");
    return Str(RtosFormat(x, static_cast<int>(mode), static_cast<int>(prec)));
  };
  // (cvunit value from-unit to-unit): a number or a list of numbers; nil for an unknown or incompatible unit.
  t["cvunit"] = [](Interp&, List& a) {
    NeedArgs("cvunit", a, 3, 3);
    const std::string& from = NeedStr("cvunit", a[1]);
    const std::string& to = NeedStr("cvunit", a[2]);
    if (!IsNum(a[0]) && !IsList(a[0])) Fail("bad argument type: numberp or point list: " + Interp::Format(a[0], true) + " in cvunit");
    UnitExpr f, g;
    if (!ParseUnitExpr(from, f) || !ParseUnitExpr(to, g)) return Nil();
    List vals;
    if (IsNum(a[0])) vals.push_back(a[0]); else vals = *a[0].l;
    if (vals.size() < 1 || (IsList(a[0]) && (vals.size() < 2 || vals.size() > 3))) Fail("bad argument type: a point of two or three numbers in cvunit");
    List out;
    for (const Value& v : vals) {
      double r;
      if (!CvUnit(NeedNum("cvunit", v), f, g, r)) return Nil();
      out.push_back(Real(r));
    }
    return IsNum(a[0]) ? out[0] : MakeList(out);
  };
  // (vl-list* obj [obj ...]): like list, the last argument being the tail. A tail that is a list is joined on, one atom after
  // one object makes a dotted pair; a dotted list of more terms cannot be represented by this interpreter and is refused.
  t["vl-list*"] = [](Interp&, List& a) {
    if (a.empty()) Fail("too few arguments to vl-list*");
    if (a.size() == 1) return a[0];
    const Value& tail = a.back();
    if (IsNil(tail) || (IsList(tail) && !tail.dotted)) {
      List l(a.begin(), a.end() - 1);
      for (const Value& v : Items(tail)) l.push_back(v);
      return MakeList(l);
    }
    if (a.size() == 2) return Pair(a[0], tail);
    Fail("vl-list*: a dotted list of more than two terms is not supported");
  };
  t["vl-symbolp"] = [](Interp&, List& a) {
    NeedArgs("vl-symbolp", a, 1, 1);
    return Bool(a[0].t == Value::T::Sym || a[0].t == Value::T::True);
  };
  // (vl-symbol-value sym): the value bound to the symbol; the symbol T is itself, nil is nil.
  t["vl-symbol-value"] = [](Interp& in, List& a) {
    NeedArgs("vl-symbol-value", a, 1, 1);
    if (IsNil(a[0])) return Nil();
    if (a[0].t == Value::T::True) return True();
    const std::string& n = NeedSymName("vl-symbol-value", a[0]);
    auto it = in.Globals().find(n);
    return it != in.Globals().end() ? it->second : Nil();
  };
  // (set sym value): sets the symbol named by the (evaluated) first argument.
  t["set"] = [](Interp& in, List& a) {
    NeedArgs("set", a, 2, 2);
    const std::string& n = NeedSymName("set", a[0]);
    if (IsBuiltinName(n) && n != "pi") Fail("cannot redefine the built-in function " + Upper(n));
    in.Globals()[n] = a[1];
    return a[1];
  };
  // (atoms-family format [symlist]): format 0 gives symbols, 1 strings; with a list of names nil stands for an undefined one.
  t["atoms-family"] = [](Interp& in, List& a) {
    NeedArgs("atoms-family", a, 1, 2);
    const long long fmt = NeedWhole("atoms-family", a[0]);
    if (fmt != 0 && fmt != 1) Fail("bad argument value: atoms-family format must be 0 or 1");
    static const char* kForms[] = {"quote", "function", "setq", "if", "progn", "and", "or", "cond", "while", "repeat", "foreach", "vlax-for", "lambda", "defun"};
    auto defined = [&](const std::string& n) {
      if (IsBuiltinName(n) || in.Globals().count(n)) return true;
      for (const char* f : kForms) if (n == f) return true;
      return false;
    };
    auto make = [&](const std::string& n) { return fmt == 1 ? Str(Upper(n)) : Sym(n); };
    List out;
    if (a.size() == 2) {
      for (const Value& v : NeedList("atoms-family", a[1])) {
        const std::string n = Lower(NeedStr("atoms-family", v));
        out.push_back(defined(n) ? make(n) : Nil());
      }
      return MakeList(out);
    }
    std::vector<std::string> names = BuiltinNames();
    for (const auto& kv : in.Globals()) names.push_back(kv.first);
    for (const char* f : kForms) names.push_back(f);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    for (const std::string& n : names) out.push_back(make(n));
    return MakeList(out);
  };
  // (snvalid name [bar-flag]): T when the string is a valid symbol-table name.
  t["snvalid"] = [](Interp&, List& a) {
    NeedArgs("snvalid", a, 1, 2);
    const std::string& s = NeedStr("snvalid", a[0]);
    const bool bar = a.size() == 2 && !IsNil(a[1]) && !(a[1].t == Value::T::Int && a[1].i == 0);
    return Bool(SnValid(s, bar));
  };
  // (wcmatch string pattern): case-sensitive; # digit, @ letter, . non-alphanumeric, * run, ? any one, [..] / [~..] sets, ~ first = not, ` escape, , alternatives.
  t["wcmatch"] = [](Interp&, List& a) {
    NeedArgs("wcmatch", a, 2, 2);
    return Bool(WcMatch(NeedStr("wcmatch", a[0]), NeedStr("wcmatch", a[1])));
  };
  t["vl-filename-base"] = [](Interp&, List& a) {
    NeedArgs("vl-filename-base", a, 1, 1);
    std::string s = NeedStr("vl-filename-base", a[0]);
    const size_t sep = LastSep(s);
    if (sep != std::string::npos) s.erase(0, sep + 1);
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos && dot > 0) s.erase(dot);
    return Str(FileCase(s));
  };
  t["vl-filename-directory"] = [](Interp&, List& a) {
    NeedArgs("vl-filename-directory", a, 1, 1);
    const std::string& s = NeedStr("vl-filename-directory", a[0]);
    const size_t sep = LastSep(s);
    if (sep == std::string::npos) return Str("");
    return Str(FileCase(sep == 0 ? s.substr(0, 1) : s.substr(0, sep)));
  };
  t["vl-filename-extension"] = [](Interp&, List& a) {
    NeedArgs("vl-filename-extension", a, 1, 1);
    std::string s = NeedStr("vl-filename-extension", a[0]);
    const size_t sep = LastSep(s);
    if (sep != std::string::npos) s.erase(0, sep + 1);
    const size_t dot = s.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return Nil();
    return Str(FileCase(s.substr(dot)));
  };
  // The blackboard shared by every namespace.
  t["vl-bb-set"] = [](Interp&, List& a) {
    NeedArgs("vl-bb-set", a, 2, 2);
    const std::string& n = NeedSymName("vl-bb-set", a[0]);
    if (a[1].t == Value::T::Lambda || a[1].t == Value::T::Subr) Fail("bad argument type: a function cannot be stored on the blackboard");
    Blackboard()[n] = a[1];
    return a[1];
  };
  t["vl-bb-ref"] = [](Interp&, List& a) {
    NeedArgs("vl-bb-ref", a, 1, 1);
    const std::string& n = NeedSymName("vl-bb-ref", a[0]);
    auto it = Blackboard().find(n);
    return it != Blackboard().end() ? it->second : Nil();
  };
}

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
      std::optional<std::string> tok = in.host.next_input ? in.host.next_input(msg, kind) : std::nullopt;
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
        case Value::T::Error: return Sym("vl-catch-all-apply-error");
        case Value::T::Vla: return Sym("vla-object");
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
    t["cadddr"] = [](Interp&, List& a) { return Cxr("cadddr", "addd", a); };
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
      bool found = in.host.read_file && in.host.read_file(path, text);
      const size_t slash = path.find_last_of("/\\");
      const bool has_ext = path.find('.', slash == std::string::npos ? 0 : slash) != std::string::npos;   // a dot in the file name itself
      if (!found && in.host.read_file && !has_ext) found = in.host.read_file(path + ".lsp", text);   // (load "tools")
      if (!found) {
        if (a.size() == 2) return a[1];   // (load "f" onfailure)
        Fail("can't open file: " + path);
      }
      if (a.size() == 2) {
        try { return in.EvalText(text); } catch (const LispError&) { return a[1]; }
      }
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
      // (ssget "X" [filter])  (ssget "_L")  (ssget "W"|"C" p1 p2 [filter])  (ssget "WP"|"CP"|"F" points [filter])
      static const char* kUsage = "ssget: the modes are \"X\" [filter], \"_L\", \"W\"/\"C\" p1 p2 [filter], \"WP\"/\"CP\"/\"F\" points [filter]";
      if (a.empty() || a[0].t != Value::T::Str) Fail(kUsage);
      std::string mode = Upper(a[0].s);
      if (!mode.empty() && mode[0] == '_') mode.erase(0, 1);
      Value filter = Nil();
      std::vector<P2> poly;
      char wmode = 0;
      if (mode == "X") {
        if (a.size() > 2) Fail("ssget \"X\" takes at most a filter list");
        if (a.size() == 2) filter = a[1];
      } else if (mode == "L") {
        if (a.size() != 1) Fail("ssget \"_L\" takes no further arguments");
        if (!in.host.last_entity) return Nil();
        std::optional<long long> id = in.host.last_entity();
        return id ? MakeSet({Int(*id)}) : Nil();
      } else if (mode == "W" || mode == "C") {
        if (a.size() < 3 || a.size() > 4) Fail("ssget \"" + mode + "\" needs two corner points: (ssget \"" + mode + "\" p1 p2 [filter])");
        const P2 p1 = NeedPt2("ssget", a[1]), p2 = NeedPt2("ssget", a[2]);
        const double x0 = std::min(p1[0], p2[0]), x1 = std::max(p1[0], p2[0]), y0 = std::min(p1[1], p2[1]), y1 = std::max(p1[1], p2[1]);
        if (x0 == x1 || y0 == y1) Fail("ssget \"" + mode + "\": the two corners must span an area");
        poly = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        wmode = mode[0];
        if (a.size() == 4) filter = a[3];
      } else if (mode == "WP" || mode == "CP" || mode == "F") {
        if (a.size() < 2 || a.size() > 3) Fail("ssget \"" + mode + "\" needs a list of points: (ssget \"" + mode + "\" points [filter])");
        for (const Value& pt : NeedList("ssget", a[1])) poly.push_back(NeedPt2("ssget", pt));
        if (poly.size() < (mode == "F" ? 2u : 3u)) Fail("ssget \"" + mode + "\" needs at least " + (mode == "F" ? "two" : "three") + " points");
        wmode = mode == "WP" ? 'W' : mode == "CP" ? 'C' : 'F';
        if (a.size() == 3) filter = a[2];
      } else {
        Fail(kUsage);
      }
      if (!in.host.all_entities) return Nil();
      const bool filtered = !IsNil(filter);
      if (filtered) NeedList("ssget", filter);
      if (filtered && !in.host.ent_get) Fail("entity access is not available here");
      if (wmode && !in.host.ent_outline) Fail("windowed selection is not available here");
      List ids;
      for (long long id : in.host.all_entities()) {
        if (wmode) {
          Outline out;
          if (!in.host.ent_outline(id, out) || !OutlineSelected(out, poly, wmode)) continue;
        }
        if (filtered) {
          std::vector<DxfItem> items;
          if (!in.host.ent_get(id, items)) continue;
          List ent;
          for (const DxfItem& d : items) ent.push_back(DxfToValue(d));
          if (!EntMatches(ent, Items(filter))) continue;
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
      if (id < 0) return Nil();   // a symbol table record is not a drawing entity
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
      const std::vector<DxfItem> items = ValueToDxf("entmod", a[0]);
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
    t["entmake"] = [](Interp& in, List& a) {
      NeedArgs("entmake", a, 1, 1);
      const std::vector<DxfItem> items = ValueToDxf("entmake", a[0]);
      if (!in.host.ent_make) return Nil();
      const std::string why = in.host.ent_make(items);
      if (!why.empty()) { in.Emit("entmake: " + why + "\n"); return Nil(); }
      return a[0];
    };
    t["entupd"] = [](Interp& in, List& a) {
      NeedArgs("entupd", a, 1, 1);
      const long long id = NeedEname("entupd", a[0]);
      return in.host.ent_upd && in.host.ent_upd(id) ? Int(id) : Nil();
    };
    // errors: (error "msg") stops the program; *error* and vl-catch-all-apply handle it
    t["error"] = [](Interp&, List& a) { NeedArgs("error", a, 1, 1); Fail(NeedStr("error", a[0])); return Nil(); };
    t["vl-catch-all-apply"] = [](Interp& in, List& a) {
      NeedArgs("vl-catch-all-apply", a, 2, 2);
      const List args = NeedList("vl-catch-all-apply", a[1]);
      try {
        return in.Apply(a[0], args);
      } catch (const LispError& e) {
        Value v;
        v.t = Value::T::Error;
        v.s = e.what();
        return v;
      }
    };
    t["vl-catch-all-error-p"] = [](Interp&, List& a) { NeedArgs("vl-catch-all-error-p", a, 1, 1); return Bool(a[0].t == Value::T::Error); };
    t["vl-catch-all-error-message"] = [](Interp&, List& a) {
      NeedArgs("vl-catch-all-error-message", a, 1, 1);
      if (a[0].t != Value::T::Error) Fail("bad argument type: vl-catch-all-apply-error: " + Interp::Format(a[0], true) + " in vl-catch-all-error-message");
      return Str(a[0].s);
    };
    t["boundp"] = [](Interp& in, List& a) {
      NeedArgs("boundp", a, 1, 1);
      if (a[0].t != Value::T::Sym) Fail("bad argument type: symbolp: " + Interp::Format(a[0], true) + " in boundp");
      auto it = in.Globals().find(a[0].s);
      return Bool(it != in.Globals().end() && !IsNil(it->second));
    };
    t["angle"] = [](Interp&, List& a) {
      NeedArgs("angle", a, 2, 2);
      const List& p = NeedList("angle", a[0]); const List& q = NeedList("angle", a[1]);
      if (p.size() < 2 || q.size() < 2) Fail("bad point argument");
      double r = std::atan2(NeedNum("angle", q[1]) - NeedNum("angle", p[1]), NeedNum("angle", q[0]) - NeedNum("angle", p[0]));
      if (r < 0) r += 2 * 3.14159265358979323846;
      return Real(r);
    };
    // system variables and symbol tables
    t["getvar"] = [](Interp& in, List& a) {
      NeedArgs("getvar", a, 1, 1);
      DxfItem d;
      if (!in.host.getvar || !in.host.getvar(Upper(NeedStr("getvar", a[0])), d)) return Nil();
      return ItemValue(d);
    };
    t["setvar"] = [](Interp& in, List& a) {
      NeedArgs("setvar", a, 2, 2);
      const std::string name = Upper(NeedStr("setvar", a[0]));
      const DxfItem d = ValueItem("setvar", a[1]);
      if (!in.host.setvar) Fail("system variables are not available here");
      const std::string why = in.host.setvar(name, d);
      if (!why.empty()) Fail("setvar " + name + ": " + why);
      return a[1];
    };
    t["tblsearch"] = [](Interp& in, List& a) {
      NeedArgs("tblsearch", a, 2, 3);
      const std::string table = Upper(NeedStr("tblsearch", a[0])), want = Lower(NeedStr("tblsearch", a[1]));
      if (!in.host.tbl_at) Fail("symbol tables are not available here");
      for (int k = 0;; ++k) {
        std::vector<DxfItem> items;
        long long id = 0;
        const int st = in.host.tbl_at(table, k, items, id);
        if (st < 0) Fail("symbol table " + table + " is not supported (LAYER, BLOCK)");
        if (st == 0) return Nil();
        const DxfItem* nm = GroupOf(items, 2);
        if (nm && Lower(nm->s) == want) {
          if (a.size() == 3 && Truthy(a[2])) in.tbl_next_[table] = k + 1;
          List out;
          for (const DxfItem& d : items) out.push_back(DxfToValue(d));
          return MakeList(out);
        }
      }
    };
    t["tblnext"] = [](Interp& in, List& a) {
      NeedArgs("tblnext", a, 1, 2);
      const std::string table = Upper(NeedStr("tblnext", a[0]));
      if (!in.host.tbl_at) Fail("symbol tables are not available here");
      if (a.size() == 2 && Truthy(a[1])) in.tbl_next_[table] = 0;
      const int k = in.tbl_next_[table];
      std::vector<DxfItem> items;
      long long id = 0;
      const int st = in.host.tbl_at(table, k, items, id);
      if (st < 0) Fail("symbol table " + table + " is not supported (LAYER, BLOCK)");
      if (st == 0) return Nil();
      in.tbl_next_[table] = k + 1;
      List out;
      for (const DxfItem& d : items) out.push_back(DxfToValue(d));
      return MakeList(out);
    };
    t["tblobjname"] = [](Interp& in, List& a) {
      NeedArgs("tblobjname", a, 2, 2);
      const std::string table = Upper(NeedStr("tblobjname", a[0])), want = Lower(NeedStr("tblobjname", a[1]));
      if (!in.host.tbl_at) Fail("symbol tables are not available here");
      for (int k = 0;; ++k) {
        std::vector<DxfItem> items;
        long long id = 0;
        const int st = in.host.tbl_at(table, k, items, id);
        if (st < 0) Fail("symbol table " + table + " is not supported (LAYER, BLOCK)");
        if (st == 0) return Nil();
        const DxfItem* nm = GroupOf(items, 2);
        if (nm && Lower(nm->s) == want) return Int(id);
      }
    };
    // vlax-* / vla-*: an object model over the entity model
    t["vlax-get-acad-object"] = [](Interp&, List& a) { NeedArgs("vlax-get-acad-object", a, 0, 0); return MakeVla("app"); };
    t["vlax-ename->vla-object"] = [](Interp& in, List& a) {
      NeedArgs("vlax-ename->vla-object", a, 1, 1);
      const long long id = NeedEname("vlax-ename->vla-object", a[0]);
      if (id <= 0) Fail("bad argument type: a drawing entity name in vlax-ename->vla-object");
      LoadEnt(in, id);
      return MakeVla("ent", id);
    };
    t["vlax-vla-object->ename"] = [](Interp&, List& a) {
      NeedArgs("vlax-vla-object->ename", a, 1, 1);
      if (a[0].t != Value::T::Vla || a[0].s != "ent") Fail("bad argument type: VLA entity object in vlax-vla-object->ename");
      return Int(a[0].i);
    };
    t["vlax-get-property"] = [](Interp& in, List& a) {
      NeedArgs("vlax-get-property", a, 2, 2);
      if (a[0].t != Value::T::Vla) Fail("bad argument type: VLA-OBJECT in vlax-get-property");
      return VlaGetProperty(in, a[0], NeedStr("vlax-get-property", a[1]));
    };
    t["vlax-put-property"] = [](Interp& in, List& a) {
      NeedArgs("vlax-put-property", a, 3, 3);
      if (a[0].t != Value::T::Vla) Fail("bad argument type: VLA-OBJECT in vlax-put-property");
      return VlaPutProperty(in, a[0], NeedStr("vlax-put-property", a[1]), a[2]);
    };
    t["vlax-invoke-method"] = [](Interp& in, List& a) {
      if (a.size() < 2) Fail("too few arguments to vlax-invoke-method");
      if (a[0].t != Value::T::Vla) Fail("bad argument type: VLA-OBJECT in vlax-invoke-method");
      return VlaInvoke(in, a[0], NeedStr("vlax-invoke-method", a[1]), List(a.begin() + 2, a.end()));
    };
    t["vlax-invoke"] = t["vlax-invoke-method"];
    t["vlax-property-available-p"] = [](Interp& in, List& a) {
      NeedArgs("vlax-property-available-p", a, 2, 3);
      if (a[0].t != Value::T::Vla) Fail("bad argument type: VLA-OBJECT in vlax-property-available-p");
      try { VlaGetProperty(in, a[0], NeedStr("vlax-property-available-p", a[1])); } catch (const LispError& e) {
        const std::string m = e.what();
        if (m.find("has no property") != std::string::npos) return Nil();   // not a property of this object
        throw;
      }
      if (a.size() == 3 && Truthy(a[2])) {   // writable too?
        static const char* kReadOnly[] = {"length", "area", "arclength", "totalangle", "circumference", "angle", "delta", "normal", "objectname", "handle", "objectid",
                                          "count", "activedocument", "modelspace", "application", "name", "majorradius", "minorradius", "radiusratio", "majoraxis",
                                          "numberofcontrolpoints", "controlpoints", "degree"};
        const std::string p = Lower(a[1].s);
        for (const char* r : kReadOnly) if (p == r) return Nil();
        if (a[0].s == "ent") {
          const std::vector<DxfItem> g = LoadEnt(in, a[0].i);
          const std::string type = GroupStr(g, 0);
          if ((p == "startpoint" || p == "endpoint") && type == "ARC") return Nil();
          if ((p == "startangle" || p == "endangle") && type == "ELLIPSE") return Nil();
          if (type == "ELLIPSE" && p != "layer" && p != "color") return Nil();
        }
      }
      return True();
    };
    t["vlax-erased-p"] = [](Interp& in, List& a) {
      NeedArgs("vlax-erased-p", a, 1, 1);
      if (a[0].t != Value::T::Vla || a[0].s != "ent") Fail("bad argument type: VLA entity object in vlax-erased-p");
      std::vector<DxfItem> g;
      return Bool(!(in.host.ent_get && in.host.ent_get(a[0].i, g)));
    };
    t["vlax-release-object"] = [](Interp&, List& a) { NeedArgs("vlax-release-object", a, 1, 1); return Nil(); };
    t["vlax-3d-point"] = [](Interp&, List& a) {
      if (a.size() == 1) return Pt3Value(NeedP3("vlax-3d-point", a[0]));
      NeedArgs("vlax-3d-point", a, 2, 3);
      return Pt3Value({NeedNum("vlax-3d-point", a[0]), NeedNum("vlax-3d-point", a[1]), a.size() == 3 ? NeedNum("vlax-3d-point", a[2]) : 0.0});
    };
    t["vlax-make-variant"] = [](Interp&, List& a) { NeedArgs("vlax-make-variant", a, 1, 2); return a[0]; };
    t["vlax-variant-value"] = [](Interp&, List& a) { NeedArgs("vlax-variant-value", a, 1, 1); return a[0]; };
    t["vlax-safearray->list"] = [](Interp&, List& a) { NeedArgs("vlax-safearray->list", a, 1, 1); NeedList("vlax-safearray->list", a[0]); return a[0]; };
    // vlax-curve-*: the parameter is the NURBS parameter of the stored curve (a circle's is not the angle)
    t["vlax-curve-getstartparam"] = [](Interp& in, List& a) { NeedArgs("vlax-curve-getStartParam", a, 1, 1); return Real(CurveOp(in, "vlax-curve-getStartParam", a[0], "startparam", {})[0]); };
    t["vlax-curve-getendparam"] = [](Interp& in, List& a) { NeedArgs("vlax-curve-getEndParam", a, 1, 1); return Real(CurveOp(in, "vlax-curve-getEndParam", a[0], "endparam", {})[0]); };
    t["vlax-curve-getstartpoint"] = [](Interp& in, List& a) {
      NeedArgs("vlax-curve-getStartPoint", a, 1, 1);
      const double t0 = CurveOp(in, "vlax-curve-getStartPoint", a[0], "startparam", {})[0];
      const std::vector<double> o = CurveOp(in, "vlax-curve-getStartPoint", a[0], "pointat", {t0});
      return Pt3Value({o[0], o[1], o[2]});
    };
    t["vlax-curve-getendpoint"] = [](Interp& in, List& a) {
      NeedArgs("vlax-curve-getEndPoint", a, 1, 1);
      const double t1 = CurveOp(in, "vlax-curve-getEndPoint", a[0], "endparam", {})[0];
      const std::vector<double> o = CurveOp(in, "vlax-curve-getEndPoint", a[0], "pointat", {t1});
      return Pt3Value({o[0], o[1], o[2]});
    };
    t["vlax-curve-getpointatparam"] = [](Interp& in, List& a) {
      NeedArgs("vlax-curve-getPointAtParam", a, 2, 2);
      const std::vector<double> o = CurveOp(in, "vlax-curve-getPointAtParam", a[0], "pointat", {NeedNum("vlax-curve-getPointAtParam", a[1])});
      return Pt3Value({o[0], o[1], o[2]});
    };
    t["vlax-curve-getdistatparam"] = [](Interp& in, List& a) {
      NeedArgs("vlax-curve-getDistAtParam", a, 2, 2);
      return Real(CurveOp(in, "vlax-curve-getDistAtParam", a[0], "distatparam", {NeedNum("vlax-curve-getDistAtParam", a[1])})[0]);
    };
    t["vlax-curve-getpointatdist"] = [](Interp& in, List& a) {
      NeedArgs("vlax-curve-getPointAtDist", a, 2, 2);
      const std::vector<double> o = CurveOp(in, "vlax-curve-getPointAtDist", a[0], "pointatdist", {NeedNum("vlax-curve-getPointAtDist", a[1])});
      return Pt3Value({o[0], o[1], o[2]});
    };
    t["vlax-curve-getparamatdist"] = [](Interp& in, List& a) {
      NeedArgs("vlax-curve-getParamAtDist", a, 2, 2);
      return Real(CurveOp(in, "vlax-curve-getParamAtDist", a[0], "paramatdist", {NeedNum("vlax-curve-getParamAtDist", a[1])})[0]);
    };
    t["vlax-curve-getclosestpointto"] = [](Interp& in, List& a) {
      NeedArgs("vlax-curve-getClosestPointTo", a, 2, 3);
      const P3 p = NeedP3("vlax-curve-getClosestPointTo", a[1]);
      const std::vector<double> o = CurveOp(in, "vlax-curve-getClosestPointTo", a[0], "closestpoint", {p[0], p[1], p[2]});
      return Pt3Value({o[0], o[1], o[2]});
    };
    // a point that is on the curve gives its parameter / distance; one that is not gives nil
    t["vlax-curve-getparamatpoint"] = [](Interp& in, List& a) {
      NeedArgs("vlax-curve-getParamAtPoint", a, 2, 2);
      const P3 p = NeedP3("vlax-curve-getParamAtPoint", a[1]);
      const std::vector<double> o = CurveOp(in, "vlax-curve-getParamAtPoint", a[0], "paramatpoint", {p[0], p[1], p[2]});
      return o[1] > 1e-6 * (1 + std::fabs(p[0]) + std::fabs(p[1])) ? Nil() : Real(o[0]);
    };
    t["vlax-curve-getdistatpoint"] = [](Interp& in, List& a) {
      NeedArgs("vlax-curve-getDistAtPoint", a, 2, 2);
      const P3 p = NeedP3("vlax-curve-getDistAtPoint", a[1]);
      const std::vector<double> o = CurveOp(in, "vlax-curve-getDistAtPoint", a[0], "paramatpoint", {p[0], p[1], p[2]});
      if (o[1] > 1e-6 * (1 + std::fabs(p[0]) + std::fabs(p[1]))) return Nil();
      return Real(CurveOp(in, "vlax-curve-getDistAtPoint", a[0], "distatparam", {o[0]})[0]);
    };
    t["vlax-curve-isclosed"] = [](Interp& in, List& a) { NeedArgs("vlax-curve-isClosed", a, 1, 1); return Bool(CurveOp(in, "vlax-curve-isClosed", a[0], "isclosed", {})[0] != 0); };
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
    // ---- bitwise and number-theory functions (AutoLISP integers: whole numbers; lsh works on 32 bits) ----
    // (~ int): the one's complement.
    t["~"] = [](Interp&, List& a) {
      NeedArgs("~", a, 1, 1);
      return Int(~NeedInt("~", a[0]));
    };
    // (logand [int ...]) and (logior [int ...]): bitwise AND / inclusive OR of all arguments, 0 with none.
    t["logand"] = [](Interp&, List& a) {
      if (a.empty()) return Int(0);
      long long r = NeedInt("logand", a[0]);
      for (size_t k = 1; k < a.size(); ++k) r &= NeedInt("logand", a[k]);
      return Int(r);
    };
    t["logior"] = [](Interp&, List& a) {
      long long r = 0;
      for (const Value& v : a) r |= NeedInt("logior", v);
      return Int(r);
    };
    // (lsh int numbits): shifts the 32-bit pattern of int left (positive) or right (negative), filling with zero
    // bits and dropping what leaves the word; the result is read back as a signed 32-bit integer.
    t["lsh"] = [](Interp&, List& a) {
      NeedArgs("lsh", a, 0, 2);
      if (a.empty()) return Int(0);
      const std::uint32_t w = static_cast<std::uint32_t>(NeedInt("lsh", a[0]));
      const long long n = a.size() == 2 ? NeedInt("lsh", a[1]) : 0;
      std::uint32_t r = w;
      if (n >= 32 || n <= -32) r = 0;
      else if (n > 0) r = w << n;
      else if (n < 0) r = w >> (-n);
      return Int(static_cast<long long>(static_cast<std::int32_t>(r)));
    };
    // (gcd int1 int2): greatest common divisor of two positive integers.
    t["gcd"] = [](Interp&, List& a) {
      NeedArgs("gcd", a, 2, 2);
      long long x = NeedInt("gcd", a[0]), y = NeedInt("gcd", a[1]);
      if (x <= 0 || y <= 0) Fail("bad argument value: gcd needs positive integers");
      while (y != 0) { const long long r = x % y; x = y; y = r; }
      return Int(x);
    };
    // (boole operator int1 [int2 ...]): operator 0-15 is a truth table, bit 1 for (1,1), bit 2 for (1,0), bit 4 for (0,1),
    // bit 8 for (0,0) of the two argument bits; the function is applied across the arguments from the left.
    t["boole"] = [](Interp&, List& a) {
      if (a.size() < 2) Fail("too few arguments to boole");
      const long long op = NeedInt("boole", a[0]);
      if (op < 0 || op > 15) Fail("bad argument value: boole operator must be 0 to 15");
      long long r = NeedInt("boole", a[1]);
      for (size_t k = 2; k < a.size(); ++k) {
        const long long b = NeedInt("boole", a[k]);
        long long out = 0;
        if (op & 1) out |= r & b;
        if (op & 2) out |= r & ~b;
        if (op & 4) out |= ~r & b;
        if (op & 8) out |= ~r & ~b;
        r = out;
      }
      return Int(r);
    };
    // ---- list and symbol utilities ----
    t["acad_strlsort"] = [](Interp&, List& a) {
      NeedArgs("acad_strlsort", a, 1, 1);
      if (!IsList(a[0])) return Nil();
      std::vector<std::string> v;
      for (const Value& x : *a[0].l) { if (x.t != Value::T::Str) return Nil(); v.push_back(x.s); }
      std::stable_sort(v.begin(), v.end());
      List out;
      for (std::string& x : v) out.push_back(Str(std::move(x)));
      return MakeList(out);
    };
    t["vl-consp"] = [](Interp&, List& a) { NeedArgs("vl-consp", a, 1, 1); return Bool(IsList(a[0])); };
    t["vl-list-length"] = [](Interp&, List& a) {
      NeedArgs("vl-list-length", a, 1, 1);
      if (!IsList(a[0]) && !IsNil(a[0])) Fail("bad argument type: listp: " + Interp::Format(a[0], true) + " in vl-list-length");
      if (a[0].dotted) return Nil();
      return Int(static_cast<long long>(Items(a[0]).size()));
    };
    t["vl-member-if-not"] = [](Interp& in, List& a) {
      NeedArgs("vl-member-if-not", a, 2, 2);
      const List src = NeedList("vl-member-if-not", a[1]);
      for (size_t k = 0; k < src.size(); ++k) if (!Truthy(in.Apply(a[0], {src[k]}))) return MakeList(List(src.begin() + static_cast<long>(k), src.end()));
      return Nil();
    };
    // (vl-sort-i list less-p): like vl-sort but returns the original zero-based indexes in sorted order.
    t["vl-sort-i"] = [](Interp& in, List& a) {
      NeedArgs("vl-sort-i", a, 2, 2);
      const List src = NeedList("vl-sort-i", a[0]);
      const size_t n = src.size();
      std::vector<size_t> cur(n);
      for (size_t k = 0; k < n; ++k) cur[k] = k;
      for (size_t w = 1; w < n; w *= 2) {
        std::vector<size_t> next;
        for (size_t lo = 0; lo < n; lo += 2 * w) {
          const size_t mid = std::min(lo + w, n), hi = std::min(lo + 2 * w, n);
          size_t i = lo, j = mid;
          while (i < mid && j < hi) {
            if (Truthy(in.Apply(a[1], {src[cur[j]], src[cur[i]]}))) next.push_back(cur[j++]); else next.push_back(cur[i++]);
          }
          while (i < mid) next.push_back(cur[i++]);
          while (j < hi) next.push_back(cur[j++]);
        }
        cur = std::move(next);
      }
      List out;
      for (size_t k : cur) out.push_back(Int(static_cast<long long>(k)));
      return MakeList(out);
    };
    // (vl-string-elt string position): the character code at a zero-based position.
    t["vl-string-elt"] = [](Interp&, List& a) {
      NeedArgs("vl-string-elt", a, 2, 2);
      const std::string& s = NeedStr("vl-string-elt", a[0]);
      const long long p = NeedInt("vl-string-elt", a[1]);
      if (p < 0 || static_cast<size_t>(p) >= s.size()) Fail("vl-string-elt: position " + std::to_string(p) + " is outside the string");
      return Int(static_cast<unsigned char>(s[static_cast<size_t>(p)]));
    };
    // (vl-string-mismatch str1 str2 [pos1 pos2 ignore-case-p]): how many characters match, counting from pos1 / pos2.
    t["vl-string-mismatch"] = [](Interp&, List& a) {
      NeedArgs("vl-string-mismatch", a, 2, 5);
      const std::string& s1 = NeedStr("vl-string-mismatch", a[0]);
      const std::string& s2 = NeedStr("vl-string-mismatch", a[1]);
      long long p1 = 0, p2 = 0;
      if (a.size() > 2 && !IsNil(a[2])) p1 = NeedInt("vl-string-mismatch", a[2]);
      if (a.size() > 3 && !IsNil(a[3])) p2 = NeedInt("vl-string-mismatch", a[3]);
      const bool nocase = a.size() > 4 && Truthy(a[4]);
      if (p1 < 0 || p2 < 0 || static_cast<size_t>(p1) > s1.size() || static_cast<size_t>(p2) > s2.size()) Fail("bad argument value: start position in vl-string-mismatch");
      long long n = 0;
      while (static_cast<size_t>(p1 + n) < s1.size() && static_cast<size_t>(p2 + n) < s2.size()) {
        unsigned char c1 = static_cast<unsigned char>(s1[static_cast<size_t>(p1 + n)]);
        unsigned char c2 = static_cast<unsigned char>(s2[static_cast<size_t>(p2 + n)]);
        if (nocase) { c1 = static_cast<unsigned char>(std::tolower(c1)); c2 = static_cast<unsigned char>(std::tolower(c2)); }
        if (c1 != c2) break;
        ++n;
      }
      return Int(n);
    };
    // (vl-string-translate source-set dest-set str): each character of str found in source-set becomes the character
    // at the same position in dest-set; characters without a counterpart are left alone.
    t["vl-string-translate"] = [](Interp&, List& a) {
      NeedArgs("vl-string-translate", a, 3, 3);
      const std::string& from = NeedStr("vl-string-translate", a[0]);
      const std::string& to = NeedStr("vl-string-translate", a[1]);
      std::string s = NeedStr("vl-string-translate", a[2]);
      for (char& c : s) {
        const size_t at = from.find(c);
        if (at != std::string::npos && at < to.size()) c = to[at];
      }
      return Str(s);
    };
    // (vl-symbol-name sym): the symbol's name in upper case.
    t["vl-symbol-name"] = [](Interp&, List& a) {
      NeedArgs("vl-symbol-name", a, 1, 1);
      if (a[0].t == Value::T::True) return Str("T");
      if (a[0].t != Value::T::Sym) Fail("bad argument type: symbolp: " + Interp::Format(a[0], true) + " in vl-symbol-name");
      return Str(Upper(a[0].s));
    };
    // (vl-some pred list ...) the first non-nil result; (vl-every pred list ...) T when every result is non-nil.
    t["vl-some"] = [](Interp& in, List& a) { return SomeEvery(in, "vl-some", a, true); };
    t["vl-every"] = [](Interp& in, List& a) { return SomeEvery(in, "vl-every", a, false); };
    AddRw2(t);
    return t;
  }
};

namespace {
bool IsBuiltinName(const std::string& n) { return Builtins::Table().count(n) != 0; }
std::vector<std::string> BuiltinNames() {
  std::vector<std::string> v;
  for (const auto& kv : Builtins::Table()) v.push_back(kv.first);
  return v;
}
}  // namespace

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
  // A stray ")" after the forms is a syntax error for the evaluator to report, not typed input.
  p.SkipSpace();
  if (p.pos < text.size() && text[p.pos] == ')') return;
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

// An error reaching the top level: when the user defined (defun *error* (msg) ...) it is called with the
// message and the program stops there quietly, as in AutoLISP; otherwise the error goes on to the host.
Value Interp::HandleTopError(const LispError& e) {
  auto it = globals_.find("*error*");
  if (in_error_handler_ || it == globals_.end() || it->second.t != Value::T::Lambda) throw e;
  struct Guard { bool& f; ~Guard() { f = false; } } guard{in_error_handler_};
  in_error_handler_ = true;
  steps_ = 0;
  const Value fn = it->second;
  Apply(fn, {Str(e.what())});
  return Nil();
}

Value Interp::EvalText(const std::string& text_in) {
  std::string text = text_in;
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);   // a UTF-8 byte order mark
  List forms = ParseAll(text);   // a syntax error anywhere runs nothing
  const bool top = depth_ == 0;
  if (top) steps_ = 0;
  Value last = Nil();
  try {
    for (const Value& f : forms) last = Eval(f);
  } catch (const LispError& e) {
    if (!top) throw;
    return HandleTopError(e);
  }
  return last;
}

bool Interp::IsDefined(const std::string& name) const {
  auto it = globals_.find(Lower(name));
  return it != globals_.end() && it->second.t == Value::T::Lambda;
}

Value Interp::CallNamed(const std::string& name) {
  auto it = globals_.find(Lower(name));
  if (it == globals_.end() || it->second.t != Value::T::Lambda) Fail("no function definition: " + Upper(name));
  const bool top = depth_ == 0;
  if (top) steps_ = 0;
  try {
    return Apply(it->second, {});
  } catch (const LispError& e) {
    if (!top) throw;
    return HandleTopError(e);
  }
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
    if (fn.s.rfind("vla-", 0) == 0) return VlaDispatch(*this, fn.s, args);
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
    Value r = Nil();   // the value of the last body form evaluated, nil when the body never ran
    while (Truthy(Eval(items[1]))) for (size_t k = 2; k < items.size(); ++k) r = Eval(items[k]);
    return r;
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
  if (n == "vlax-for") {
    // (vlax-for sym collection body...): the collection is the ModelSpace
    if (items.size() < 3 || items[1].t != Value::T::Sym) Fail("vlax-for needs a symbol and a collection");
    const Value coll = Eval(items[2]);
    if (coll.t != Value::T::Vla || coll.s != "ms") Fail("vlax-for: only the ModelSpace collection is supported");
    const std::vector<long long> ids = host.all_entities ? host.all_entities() : std::vector<long long>();
    const std::string var = items[1].s;
    auto it = globals_.find(var);
    const bool had = it != globals_.end();
    Value old = had ? it->second : Nil();
    struct R { std::map<std::string, Value>& g; std::string v; bool had; Value old; ~R() { if (had) g[v] = old; else g.erase(v); } } rr{globals_, var, had, old};
    Value r = Nil();
    for (long long id : ids) { globals_[var] = MakeVla("ent", id); for (size_t k = 3; k < items.size(); ++k) r = Eval(items[k]); }
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
        name == "cond" || name == "while" || name == "repeat" || name == "foreach" || name == "vlax-for" || name == "lambda" || name == "defun" || name == "function")
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
  if (n.rfind("vla-", 0) == 0) return VlaDispatch(*this, n, args);
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
    case Value::T::Error: return "#<%catch-all-apply-error%>";
    case Value::T::Vla: {
      if (v.s == "app") return "#<VLA-OBJECT IAcadApplication 0000000000000001>";
      if (v.s == "doc") return "#<VLA-OBJECT IAcadDocument 0000000000000002>";
      if (v.s == "ms") return "#<VLA-OBJECT IAcadModelSpace 0000000000000003>";
      char b[64];
      std::snprintf(b, sizeof b, "#<VLA-OBJECT ENTITY %016llx>", static_cast<unsigned long long>(v.i));
      return b;
    }
  }
  return "";
}

}  // namespace dino8::lisp
