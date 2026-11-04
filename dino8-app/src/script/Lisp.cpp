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
      if (a.l->size() != b.l->size()) return false;
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
        items.push_back(ParseOne());
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
      else cur = MakeList(List(l.begin() + 1, l.end()));
    }
    return cur;
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
        case Value::T::List: return Sym("list");
        default: return Sym("subr");
      }
    };
    // lists
    t["list"] = [](Interp&, List& a) { return MakeList(a); };
    t["cons"] = [](Interp&, List& a) {
      NeedArgs("cons", a, 2, 2);
      if (!IsList(a[1]) && !IsNil(a[1])) Fail("dotted pairs are not supported: the second argument of cons must be a list");
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
      NeedArgs("ssget", a, 0, 1);
      if (a.size() != 1 || a[0].t != Value::T::Str || Upper(a[0].s) != "X") Fail("only (ssget \"X\") is supported");
      if (!in.host.all_entities) return Nil();
      List ids;
      for (long long id : in.host.all_entities()) ids.push_back(Int(id));
      return MakeList(ids);
    };
    t["sslength"] = [](Interp&, List& a) { NeedArgs("sslength", a, 1, 1); return Int(static_cast<long long>(NeedList("sslength", a[0]).size())); };
    t["ssname"] = t["nth"];
    t["ssname"] = [](Interp&, List& a) {
      NeedArgs("ssname", a, 2, 2);
      const List& l = NeedList("ssname", a[0]);
      if (a[1].t != Value::T::Int || a[1].i < 0) Fail("bad argument type: index in ssname");
      return static_cast<size_t>(a[1].i) < l.size() ? l[static_cast<size_t>(a[1].i)] : Nil();
    };
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
