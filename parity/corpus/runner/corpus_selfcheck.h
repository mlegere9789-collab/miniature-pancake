// Independent numeric check of the manifest's closed-form expectations (no kernel code).
// Every operand kind has an implicit inside test and a surface sampler; the result of a case is
// evaluated by combining the tests as the boolean steps do.  Volume and centroid come from a
// midpoint grid over the operands' bounding box; the area of a two-operand case from uniform
// samples of both operand surfaces classified against the other operand.  A mismatch means the
// oracle (or the recipe) is wrong, not the kernel: the numbers here never touch the kernel.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "closed_forms.h"
#include "json.h"

namespace corpus {
namespace selfcheck {

using cf::PI;
using cf::V3;

struct Rng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  double u() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (s >> 11) * (1.0 / 9007199254740992.0); }
};

inline V3 jv3(const J* a) { return a ? V3{a->at(0), a->at(1), a->at(2)} : V3{}; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 rotate(V3 p, V3 axis, double ang, V3 about) {
  axis = cf::unit(axis);
  const V3 q = p - about;
  const double c = std::cos(ang), s = std::sin(ang);
  return about + q * c + cross(axis, q) * s + axis * (cf::dot(axis, q) * (1 - c));
}
// any orthonormal frame (e1, e2) perpendicular to a unit axis
inline void frame(V3 a, V3& e1, V3& e2) {
  const V3 seed = std::fabs(a.x) < 0.9 ? V3{1, 0, 0} : V3{0, 1, 0};
  e1 = cf::unit(seed - a * cf::dot(seed, a));
  e2 = cross(a, e1);
}

struct Shape {
  std::function<bool(V3)> inside;
  V3 lo, hi;
  double area = 0;
  std::function<V3(Rng&)> sample;  // uniform on the surface (empty: no sampler)
};

inline void grow(V3& lo, V3& hi, V3 p) {
  lo = {std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
  hi = {std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
}

inline Shape make_shape(const J& s) {
  Shape sh;
  const std::string kind = s.str("kind");
  sh.lo = {1e300, 1e300, 1e300};
  sh.hi = {-1e300, -1e300, -1e300};
  if (kind == "box") {
    const V3 lo = jv3(s.get("min")), hi = jv3(s.get("max"));
    const J* rot = s.get("rot");
    V3 raxis = rot ? jv3(rot->get("axis")) : V3{0, 0, 1}, rabout = rot ? jv3(rot->get("about")) : V3{};
    const double rang = rot ? rot->num("deg", 0) * PI / 180 : 0;
    for (int i = 0; i < 8; ++i) grow(sh.lo, sh.hi, rotate({i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z}, raxis, rang, rabout));
    sh.inside = [=](V3 p) {
      const V3 q = rotate(p, raxis, -rang, rabout);
      return q.x >= lo.x && q.x <= hi.x && q.y >= lo.y && q.y <= hi.y && q.z >= lo.z && q.z <= hi.z;
    };
    const double a = hi.x - lo.x, b = hi.y - lo.y, c = hi.z - lo.z;
    sh.area = 2 * (a * b + b * c + c * a);
    sh.sample = [=](Rng& r) {
      double t = r.u() * sh.area;
      const double fa[3] = {b * c, a * c, a * b};
      int f = 0;
      for (; f < 5; ++f) { const double w = fa[f % 3]; if (t < w) break; t -= w; }
      const int ax = f % 3, side = f / 3;
      V3 q{lo.x + r.u() * a, lo.y + r.u() * b, lo.z + r.u() * c};
      if (ax == 0) q.x = side ? hi.x : lo.x;
      if (ax == 1) q.y = side ? hi.y : lo.y;
      if (ax == 2) q.z = side ? hi.z : lo.z;
      return rotate(q, raxis, rang, rabout);
    };
  } else if (kind == "cylinder") {
    const V3 b0 = jv3(s.get("base")), a = cf::unit(jv3(s.get("axis")));
    const double r = s.num("radius", 1), h = s.num("height", 1);
    V3 e1, e2;
    frame(a, e1, e2);
    grow(sh.lo, sh.hi, b0);
    for (double sgn : {-1.0, 1.0}) {
      for (int k = 0; k < 2; ++k) {
        const V3 c = b0 + a * (k ? h : 0);
        grow(sh.lo, sh.hi, {c.x + sgn * r * std::sqrt(std::fmax(0, 1 - a.x * a.x)), c.y + sgn * r * std::sqrt(std::fmax(0, 1 - a.y * a.y)), c.z + sgn * r * std::sqrt(std::fmax(0, 1 - a.z * a.z))});
      }
    }
    sh.inside = [=](V3 p) {
      const V3 q = p - b0;
      const double t = cf::dot(q, a);
      if (t < 0 || t > h) return false;
      const V3 rad = q - a * t;
      return cf::dot(rad, rad) <= r * r;
    };
    sh.area = 2 * PI * r * r + 2 * PI * r * h;
    sh.sample = [=](Rng& g) {
      const double w = g.u() * (2 * PI * r * r + 2 * PI * r * h), ang = 2 * PI * g.u();
      if (w < 2 * PI * r * h) return b0 + a * (g.u() * h) + e1 * (r * std::cos(ang)) + e2 * (r * std::sin(ang));
      const double rho = r * std::sqrt(g.u());
      return b0 + a * (w < 2 * PI * r * h + PI * r * r ? 0 : h) + e1 * (rho * std::cos(ang)) + e2 * (rho * std::sin(ang));
    };
  } else if (kind == "cone") {
    const V3 ap = jv3(s.get("apex")), a = cf::unit(jv3(s.get("axis")));
    const double r = s.num("radius", 1), h = s.num("height", 1), slant = std::sqrt(r * r + h * h);
    V3 e1, e2;
    frame(a, e1, e2);
    grow(sh.lo, sh.hi, ap);
    for (double sgn : {-1.0, 1.0}) {
      const V3 c = ap + a * h;
      grow(sh.lo, sh.hi, {c.x + sgn * r * std::sqrt(std::fmax(0, 1 - a.x * a.x)), c.y + sgn * r * std::sqrt(std::fmax(0, 1 - a.y * a.y)), c.z + sgn * r * std::sqrt(std::fmax(0, 1 - a.z * a.z))});
    }
    sh.inside = [=](V3 p) {
      const V3 q = p - ap;
      const double t = cf::dot(q, a);
      if (t < 0 || t > h) return false;
      const V3 rad = q - a * t;
      const double rr = r * t / h;
      return cf::dot(rad, rad) <= rr * rr;
    };
    sh.area = PI * r * r + PI * r * slant;
    sh.sample = [=](Rng& g) {
      const double ang = 2 * PI * g.u();
      if (g.u() * (PI * r * r + PI * r * slant) < PI * r * slant) {
        const double t = h * std::sqrt(g.u());
        return ap + a * t + e1 * (r * t / h * std::cos(ang)) + e2 * (r * t / h * std::sin(ang));
      }
      const double rho = r * std::sqrt(g.u());
      return ap + a * h + e1 * (rho * std::cos(ang)) + e2 * (rho * std::sin(ang));
    };
  } else if (kind == "sphere") {
    const V3 c = jv3(s.get("center"));
    const double r = s.num("radius", 1);
    grow(sh.lo, sh.hi, c - V3{r, r, r});
    grow(sh.lo, sh.hi, c + V3{r, r, r});
    sh.inside = [=](V3 p) { return cf::len(p - c) <= r; };
    sh.area = 4 * PI * r * r;
    sh.sample = [=](Rng& g) {
      const double z = 2 * g.u() - 1, ang = 2 * PI * g.u(), q = std::sqrt(1 - z * z);
      return c + V3{q * std::cos(ang), q * std::sin(ang), z} * r;
    };
  } else if (kind == "torus") {
    const V3 c = jv3(s.get("center")), a = cf::unit(jv3(s.get("axis")));
    const double R = s.num("major", 3), r = s.num("minor", 1);
    V3 e1, e2;
    frame(a, e1, e2);
    for (double sgn : {-1.0, 1.0}) {
      const V3 d{(R + r) * std::sqrt(std::fmax(0, 1 - a.x * a.x)) + r * std::fabs(a.x), (R + r) * std::sqrt(std::fmax(0, 1 - a.y * a.y)) + r * std::fabs(a.y),
                 (R + r) * std::sqrt(std::fmax(0, 1 - a.z * a.z)) + r * std::fabs(a.z)};
      grow(sh.lo, sh.hi, c + d * sgn);
    }
    sh.inside = [=](V3 p) {
      const V3 q = p - c;
      const double z = cf::dot(q, a);
      const V3 rad = q - a * z;
      const double rho = cf::len(rad);
      return (rho - R) * (rho - R) + z * z <= r * r;
    };
    sh.area = 4 * PI * PI * R * r;
    sh.sample = [=](Rng& g) {
      double v;
      do { v = 2 * PI * g.u(); } while (g.u() * (R + r) > R + r * std::cos(v));
      const double u = 2 * PI * g.u();
      return c + e1 * ((R + r * std::cos(v)) * std::cos(u)) + e2 * ((R + r * std::cos(v)) * std::sin(u)) + a * (r * std::sin(v));
    };
  } else if (kind == "prism") {
    std::vector<std::pair<double, double>> poly;
    for (const J& p : s.get("polygon")->a) poly.push_back({p.at(0), p.at(1)});
    const double z0 = s.num("z0", 0), z1 = s.num("z1", 1);
    for (const auto& p : poly) { grow(sh.lo, sh.hi, {p.first, p.second, z0}); grow(sh.lo, sh.hi, {p.first, p.second, z1}); }
    sh.inside = [=](V3 p) {
      if (p.z < z0 || p.z > z1) return false;
      bool in = false;
      for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].second > p.y) != (poly[j].second > p.y) && p.x < (poly[j].first - poly[i].first) * (p.y - poly[i].second) / (poly[j].second - poly[i].second) + poly[i].first) in = !in;
      return in;
    };
  } else {
    throw std::runtime_error("selfcheck: unknown kind " + kind);
  }
  return sh;
}

struct Report {
  bool area_checked = false;
  bool skipped = false;
  std::string skip_reason;
  bool mismatch = false;
  std::string detail;
};

inline Report check_case(const J& c, int grid_n) {
  Report rep;
  const J& ex = *c.get("expect");
  const J& op = *c.get("operation");
  std::map<std::string, Shape> shapes;
  V3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
  for (const J& o : c.get("operands")->a) {
    Shape sh = make_shape(o);
    grow(lo, hi, sh.lo);
    grow(lo, hi, sh.hi);
    shapes[o.str("id")] = sh;
  }
  const V3 pad = (hi - lo) * 0.01 + V3{1e-12, 1e-12, 1e-12};
  lo = lo - pad;
  hi = hi + pad;
  // result predicate
  std::map<std::string, std::function<bool(V3)>> pred;
  for (auto& kv : shapes) pred[kv.first] = kv.second.inside;
  std::function<bool(V3)> result = pred.begin()->second;
  if (op.str("type") == "measure") {
    result = pred.at(op.str("operand"));
  } else {
    for (const J& st : op.get("steps")->a) {
      std::vector<std::function<bool(V3)>> ga, gb;
      for (const J& id : st.get("a")->a) ga.push_back(pred.at(id.s));
      for (const J& id : st.get("b")->a) gb.push_back(pred.at(id.s));
      auto any = [](std::vector<std::function<bool(V3)>> g) { return std::function<bool(V3)>([g](V3 p) { for (auto& f : g) if (f(p)) return true; return false; }); };
      std::function<bool(V3)> A = any(ga), B = gb.empty() ? std::function<bool(V3)>([](V3) { return false; }) : any(gb);
      const std::string o = st.str("op");
      std::function<bool(V3)> R = o == "union" ? std::function<bool(V3)>([=](V3 p) { return A(p) || B(p); })
                                : o == "intersection" ? std::function<bool(V3)>([=](V3 p) { return A(p) && B(p); })
                                                      : std::function<bool(V3)>([=](V3 p) { return A(p) && !B(p); });
      pred[st.str("out")] = R;
      result = R;
    }
  }
  const double ev = ex.num("volume", NAN);
  const double L = cf::len(hi - lo);
  const int n = grid_n;
  const V3 d = (hi - lo) / n;
  const double cell = d.x * d.y * d.z;
  double vol = 0;
  V3 mom{};
  Rng jit;  // stratified jitter: one random point per cell, so the estimate is unbiased (a midpoint grid is biased by boundary alignment)
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
      for (int k = 0; k < n; ++k) {
        const V3 p{lo.x + (i + jit.u()) * d.x, lo.y + (j + jit.u()) * d.y, lo.z + (k + jit.u()) * d.z};
        if (result(p)) { vol += cell; mom = mom + p * cell; }
      }
  char buf[256];
  if (std::isnan(ev) || ev < 200 * cell) {
    rep.skipped = true;
    std::snprintf(buf, sizeof buf, "volume %.4g below the grid resolution (cell %.3g)", std::isnan(ev) ? 0.0 : ev, cell);
    rep.skip_reason = buf;
  } else {
    const double tolv = 3e-3 * ev + 5 * std::sqrt(ev * cell);  // oracle slack + 5 sigma of the stratified shot noise
    if (std::fabs(vol - ev) > tolv) { rep.mismatch = true; std::snprintf(buf, sizeof buf, "volume grid %.6g vs oracle %.6g", vol, ev); rep.detail += buf; }
    if (const J* ce = ex.get("centroid")) {
      const V3 c = mom / vol, e = jv3(ce);
      /* oracle slack + 5 sigma of the sampling noise of a centroid */ if (cf::len(c - e) > 4e-3 * L + 5 * L / std::sqrt(vol / cell)) { rep.mismatch = true; std::snprintf(buf, sizeof buf, "; centroid grid (%.4g,%.4g,%.4g) vs oracle (%.4g,%.4g,%.4g)", c.x, c.y, c.z, e.x, e.y, e.z); rep.detail += buf; }
    }
  }
  // area of a two-operand single-step case: classify surface samples of both operands
  const J* steps = op.get("steps");
  const std::string cat = c.str("category");
  if (ex.has("area") && steps && steps->a.size() == 1 && steps->a[0].get("a")->a.size() == 1 && steps->a[0].get("b")->a.size() == 1 && cat != "coincident" && cat != "degenerate") {
    const Shape& A = shapes.at(steps->a[0].get("a")->a[0].s);
    const Shape& B = shapes.at(steps->a[0].get("b")->a[0].s);
    if (A.sample && B.sample) {
      Rng g;
      const int ns = 1500000;
      long a_in = 0, b_in = 0;
      for (int i = 0; i < ns; ++i) { if (B.inside(A.sample(g))) ++a_in; if (A.inside(B.sample(g))) ++b_in; }
      const double fa = double(a_in) / ns, fb = double(b_in) / ns;
      const std::string o = steps->a[0].str("op");
      const double S = o == "union" ? A.area * (1 - fa) + B.area * (1 - fb) : o == "intersection" ? A.area * fa + B.area * fb : A.area * (1 - fa) + B.area * fb;
      rep.area_checked = true;
      const double ea = ex.num("area", NAN);
      if (std::fabs(S - ea) > 6e-3 * ea + 3 * (A.area + B.area) / std::sqrt(double(ns))) {
        rep.mismatch = true;
        std::snprintf(buf, sizeof buf, "; area samples %.6g vs oracle %.6g", S, ea);
        rep.detail += buf;
      }
    }
  }
  return rep;
}

}  // namespace selfcheck
}  // namespace corpus
