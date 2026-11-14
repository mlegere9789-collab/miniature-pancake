#include "util/ExprEval.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace dino8::util {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Tiny recursive-descent expression evaluator for the calculator and for
// numeric input fields (Rhino accepts "2*3" wherever a number is asked for).
class Expr {
 public:
  explicit Expr(const std::string& s) : s_(s) {}
  bool Eval(double& out, std::string& err) {
    pos_ = 0;
    ok_ = true;
    depth_ = 0;
    out = ParseAdd();
    SkipWs();
    if (ok_ && pos_ != s_.size()) { ok_ = false; err_ = "unexpected '" + std::string(1, s_[pos_]) + "'"; }
    err = err_;
    return ok_;
  }

 private:
  // Every construct that can nest arbitrarily deep - parenthesized
  // sub-expressions, a run of unary +/-, and a function call's own
  // argument list - passes through ParseUnary exactly once per nesting
  // level, so guarding it there alone catches all three: a handful of
  // bytes (e.g. 50,000 '(' characters, or 50,000 '-' characters - either
  // one well under 100 KB) used to recurse one C++ stack frame per
  // nesting level with no limit at all, reliably segfaulting via stack
  // overflow (confirmed directly before this guard existed). Reachable
  // from untrusted input: a Flow "Expression" node's text comes straight
  // from a loaded .dflow file (FlowEditor.cpp), evaluated automatically
  // the moment such a node loads, no user action beyond opening the file.
  // kMaxDepth=200 matches util/json_mini.cpp's own choice for the exact
  // same class of untrusted-nesting-depth hazard, far below the crash
  // threshold and far above anything a real expression would ever need.
  static constexpr int kMaxDepth = 200;

  void SkipWs() { while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_; }
  bool Peek(char c) { SkipWs(); return pos_ < s_.size() && s_[pos_] == c; }
  double ParseAdd() {
    double v = ParseMul();
    for (;;) {
      if (Peek('+')) { ++pos_; v += ParseMul(); }
      else if (Peek('-')) { ++pos_; v -= ParseMul(); }
      else return v;
    }
  }
  double ParseMul() {
    double v = ParsePow();
    for (;;) {
      if (Peek('*')) { ++pos_; v *= ParsePow(); }
      else if (Peek('/')) { ++pos_; double d = ParsePow(); if (d == 0) { ok_ = false; err_ = "division by zero"; return 0; } v /= d; }
      else if (Peek('%')) { ++pos_; v = std::fmod(v, ParsePow()); }
      else return v;
    }
  }
  double ParsePow() {
    double v = ParseUnary();
    if (Peek('^')) { ++pos_; v = std::pow(v, ParsePow()); }
    return v;
  }
  double ParseUnary() {
    if (++depth_ > kMaxDepth) { ok_ = false; err_ = "expression nested too deeply"; --depth_; return 0; }
    double result;
    if (Peek('-')) { ++pos_; result = -ParseUnary(); }
    else if (Peek('+')) { ++pos_; result = ParseUnary(); }
    else result = ParsePrimary();
    --depth_;
    return result;
  }
  double ParsePrimary() {
    SkipWs();
    if (pos_ >= s_.size()) { ok_ = false; err_ = "unexpected end"; return 0; }
    if (s_[pos_] == '(') {
      ++pos_;
      double v = ParseAdd();
      if (!Peek(')')) { ok_ = false; err_ = "missing ')'"; return 0; }
      ++pos_;
      return v;
    }
    if (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.') {
      size_t start = pos_;
      while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E')) ++pos_;
      return std::atof(s_.substr(start, pos_ - start).c_str());
    }
    if (std::isalpha(static_cast<unsigned char>(s_[pos_]))) {
      size_t start = pos_;
      while (pos_ < s_.size() && std::isalnum(static_cast<unsigned char>(s_[pos_]))) ++pos_;
      std::string name = s_.substr(start, pos_ - start);
      for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (name == "pi") return kPi;
      if (name == "e") return 2.718281828459045;
      if (!Peek('(')) { ok_ = false; err_ = "unknown symbol " + name; return 0; }
      ++pos_;
      std::vector<double> args;
      if (!Peek(')')) {
        args.push_back(ParseAdd());
        while (Peek(',')) { ++pos_; args.push_back(ParseAdd()); }
      }
      if (!Peek(')')) { ok_ = false; err_ = "missing ')'"; return 0; }
      ++pos_;
      auto a0 = [&]() { return args.empty() ? 0.0 : args[0]; };
      if (name == "sqrt") return std::sqrt(a0());
      if (name == "sin") return std::sin(a0() * kPi / 180.0);
      if (name == "cos") return std::cos(a0() * kPi / 180.0);
      if (name == "tan") return std::tan(a0() * kPi / 180.0);
      if (name == "asin") return std::asin(a0()) * 180.0 / kPi;
      if (name == "acos") return std::acos(a0()) * 180.0 / kPi;
      if (name == "atan") return std::atan(a0()) * 180.0 / kPi;
      if (name == "abs") return std::fabs(a0());
      if (name == "ln") return std::log(a0());
      if (name == "log") return std::log10(a0());
      if (name == "exp") return std::exp(a0());
      if (name == "floor") return std::floor(a0());
      if (name == "ceil") return std::ceil(a0());
      if (name == "round") return std::round(a0());
      if (name == "min" && args.size() >= 2) return std::min(args[0], args[1]);
      if (name == "max" && args.size() >= 2) return std::max(args[0], args[1]);
      if (name == "pow" && args.size() >= 2) return std::pow(args[0], args[1]);
      if (name == "hypot" && args.size() >= 2) return std::hypot(args[0], args[1]);
      ok_ = false;
      err_ = "unknown function " + name;
      return 0;
    }
    ok_ = false;
    err_ = "unexpected '" + std::string(1, s_[pos_]) + "'";
    return 0;
  }

  std::string s_;
  size_t pos_ = 0;
  bool ok_ = true;
  int depth_ = 0;
  std::string err_;
};

}  // namespace

bool EvaluateExpression(const std::string& text, double& out, std::string& error) {
  Expr e(text);
  return e.Eval(out, error);
}

}  // namespace dino8::util
