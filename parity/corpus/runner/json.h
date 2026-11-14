// Minimal JSON value, writer and parser for the corpus tools (header only).
// No external dependency: the manifest is plain JSON, one case per line.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace corpus {

struct J {
  enum Type { Null, Bool, Num, Str, Arr, Obj };
  Type t = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<J> a;
  std::vector<std::pair<std::string, J>> o;

  J() {}
  J(bool v) : t(Bool), b(v) {}
  J(int v) : t(Num), n(v) {}
  J(long v) : t(Num), n(static_cast<double>(v)) {}
  J(unsigned v) : t(Num), n(v) {}
  J(unsigned long v) : t(Num), n(static_cast<double>(v)) {}
  J(double v) : t(Num), n(v) {}
  J(const char* v) : t(Str), s(v) {}
  J(const std::string& v) : t(Str), s(v) {}

  static J Array() { J j; j.t = Arr; return j; }
  static J Object() { J j; j.t = Obj; return j; }
  static J A(std::initializer_list<J> l) { J j = Array(); for (const J& e : l) j.a.push_back(e); return j; }
  static J O(std::initializer_list<std::pair<std::string, J>> l) { J j = Object(); for (const auto& e : l) j.set(e.first, e.second); return j; }

  J& set(const std::string& k, J v) {
    t = Obj;
    for (auto& e : o) if (e.first == k) { e.second = std::move(v); return *this; }
    o.emplace_back(k, std::move(v));
    return *this;
  }
  J& push(J v) { t = Arr; a.push_back(std::move(v)); return *this; }
  const J* get(const std::string& k) const {
    if (t != Obj) return nullptr;
    for (const auto& e : o) if (e.first == k) return &e.second;
    return nullptr;
  }
  bool has(const std::string& k) const { return get(k) != nullptr; }
  double num(const std::string& k, double def) const { const J* p = get(k); return (p && p->t == Num) ? p->n : def; }
  bool boolean(const std::string& k, bool def) const { const J* p = get(k); return (p && p->t == Bool) ? p->b : def; }
  std::string str(const std::string& k, const std::string& def = "") const { const J* p = get(k); return (p && p->t == Str) ? p->s : def; }
  double at(size_t i) const { return (t == Arr && i < a.size()) ? a[i].n : 0.0; }

  static std::string Number(double v) {
    if (std::isnan(v)) return "null";
    if (std::isinf(v)) return v > 0 ? "1e999" : "-1e999";
    char buf[48];
    if (v == std::floor(v) && std::fabs(v) < 1e15) { std::snprintf(buf, sizeof buf, "%.0f", v); return buf; }
    for (int p = 6; p <= 17; ++p) { std::snprintf(buf, sizeof buf, "%.*g", p, v); if (std::strtod(buf, nullptr) == v) break; }
    return buf;
  }
  static void Quote(const std::string& x, std::string& out) {
    out += '"';
    for (unsigned char c : x) {
      if (c == '"') out += "\\\"";
      else if (c == '\\') out += "\\\\";
      else if (c == '\n') out += "\\n";
      else if (c == '\t') out += "\\t";
      else if (c < 0x20) { char b2[8]; std::snprintf(b2, sizeof b2, "\\u%04x", c); out += b2; }
      else out += static_cast<char>(c);
    }
    out += '"';
  }
  // Compact single-line form.
  std::string dump() const { std::string r; write(r); return r; }
  void write(std::string& r) const {
    switch (t) {
      case Null: r += "null"; break;
      case Bool: r += b ? "true" : "false"; break;
      case Num: r += Number(n); break;
      case Str: Quote(s, r); break;
      case Arr: r += '['; for (size_t i = 0; i < a.size(); ++i) { if (i) r += ','; a[i].write(r); } r += ']'; break;
      case Obj: r += '{'; for (size_t i = 0; i < o.size(); ++i) { if (i) r += ','; Quote(o[i].first, r); r += ':'; o[i].second.write(r); } r += '}'; break;
    }
  }
};

class Parser {
 public:
  explicit Parser(const std::string& text) : p_(text.c_str()), end_(text.c_str() + text.size()) {}
  J parse() { J v = value(); ws(); if (p_ != end_) fail("trailing characters"); return v; }
 private:
  const char* p_;
  const char* end_;
  [[noreturn]] void fail(const char* m) { throw std::runtime_error(std::string("json: ") + m); }
  void ws() { while (p_ < end_ && (*p_ == ' ' || *p_ == '\n' || *p_ == '\t' || *p_ == '\r')) ++p_; }
  bool lit(const char* w) { size_t n = std::char_traits<char>::length(w); if (static_cast<size_t>(end_ - p_) >= n && std::string(p_, n) == w) { p_ += n; return true; } return false; }
  std::string string() {
    if (*p_ != '"') fail("expected string");
    ++p_;
    std::string r;
    while (p_ < end_ && *p_ != '"') {
      if (*p_ == '\\') {
        ++p_;
        if (p_ >= end_) fail("bad escape");
        switch (*p_) {
          case 'n': r += '\n'; break;
          case 't': r += '\t'; break;
          case 'r': r += '\r'; break;
          case 'u': { unsigned c = 0; for (int i = 0; i < 4; ++i) { ++p_; c = c * 16 + (std::isxdigit(static_cast<unsigned char>(*p_)) ? (*p_ <= '9' ? *p_ - '0' : (*p_ | 32) - 'a' + 10) : 0); } r += static_cast<char>(c < 128 ? c : '?'); break; }
          default: r += *p_;
        }
        ++p_;
      } else r += *p_++;
    }
    if (p_ >= end_) fail("unterminated string");
    ++p_;
    return r;
  }
  J value() {
    ws();
    if (p_ >= end_) fail("unexpected end");
    if (*p_ == '{') {
      ++p_;
      J j = J::Object();
      ws();
      if (*p_ == '}') { ++p_; return j; }
      for (;;) {
        ws();
        std::string k = string();
        ws();
        if (*p_ != ':') fail("expected ':'");
        ++p_;
        j.set(k, value());
        ws();
        if (*p_ == ',') { ++p_; continue; }
        if (*p_ == '}') { ++p_; return j; }
        fail("expected ',' or '}'");
      }
    }
    if (*p_ == '[') {
      ++p_;
      J j = J::Array();
      ws();
      if (*p_ == ']') { ++p_; return j; }
      for (;;) {
        j.push(value());
        ws();
        if (*p_ == ',') { ++p_; continue; }
        if (*p_ == ']') { ++p_; return j; }
        fail("expected ',' or ']'");
      }
    }
    if (*p_ == '"') return J(string());
    if (lit("true")) return J(true);
    if (lit("false")) return J(false);
    if (lit("null")) return J();
    char* e = nullptr;
    double v = std::strtod(p_, &e);
    if (e == p_) fail("bad value");
    p_ = e;
    return J(v);
  }
};

inline J ParseJson(const std::string& text) { return Parser(text).parse(); }

}  // namespace corpus
