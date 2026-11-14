// Case generator for the Golden and Evil Corpus v0 (header only, no kernel dependency).
// Every case is emitted as a JSON object: operand recipes, an operation, and the
// expected invariants with closed-form oracles and tolerances (see corpus.json).
// All lengths are in model units.  A Frame (scale s, translation t) maps the unit
// design numbers of a configuration to the final recipe numbers, so the same
// configuration serves the golden set, the scale ladder and the far-origin ladder.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "closed_forms.h"
#include "json.h"

namespace corpus {
using cf::PI;
using cf::V3;

inline J jv(V3 v) { return J::A({v.x, v.y, v.z}); }

struct Frame {
  double s = 1;
  V3 t{0, 0, 0};
  V3 P(double x, double y, double z) const { return {t.x + s * x, t.y + s * y, t.z + s * z}; }
  V3 P(V3 v) const { return P(v.x, v.y, v.z); }
  double L(double x) const { return s * x; }
  double V(double x) const { return s * s * s * x; }
  double A(double x) const { return s * s * x; }
  double far() const { return std::fmax(std::fabs(t.x), std::fmax(std::fabs(t.y), std::fabs(t.z))); }
};

// ------------------------------------------------------------------ operand recipes
inline J op_box(const std::string& id, V3 lo, V3 hi) { return J::O({{"id", id}, {"kind", "box"}, {"min", jv(lo)}, {"max", jv(hi)}}); }
inline J op_box_rot(const std::string& id, V3 lo, V3 hi, V3 axis, double deg, V3 about) {
  J j = op_box(id, lo, hi);
  j.set("rot", J::O({{"axis", jv(axis)}, {"deg", deg}, {"about", jv(about)}}));
  return j;
}
inline J op_cyl(const std::string& id, V3 base, V3 axis, double r, double h) {
  return J::O({{"id", id}, {"kind", "cylinder"}, {"base", jv(base)}, {"axis", jv(axis)}, {"radius", r}, {"height", h}});
}
inline J op_cone(const std::string& id, V3 apex, V3 axis, double h, double r) {
  return J::O({{"id", id}, {"kind", "cone"}, {"apex", jv(apex)}, {"axis", jv(axis)}, {"height", h}, {"radius", r}});
}
inline J op_sphere(const std::string& id, V3 c, double r, V3 axis = {0, 0, 1}) {
  return J::O({{"id", id}, {"kind", "sphere"}, {"center", jv(c)}, {"radius", r}, {"axis", jv(axis)}});
}
inline J op_torus(const std::string& id, V3 c, V3 axis, double R, double r) {
  return J::O({{"id", id}, {"kind", "torus"}, {"center", jv(c)}, {"axis", jv(axis)}, {"major", R}, {"minor", r}});
}
inline J op_prism(const std::string& id, const std::vector<std::array<double, 2>>& poly, double z0, double z1) {
  J pts = J::Array();
  for (const auto& p : poly) pts.push(J::A({p[0], p[1]}));
  return J::O({{"id", id}, {"kind", "prism"}, {"polygon", pts}, {"z0", z0}, {"z1", z1}});
}

// Generic, non-special axes: poles/seams of the spheres are away from every coordinate plane.
inline V3 gen_axis1() { return {2.0 / 7, 3.0 / 7, 6.0 / 7}; }
inline V3 gen_axis2() { return {-3.0 / 7, 2.0 / 7, 6.0 / 7}; }

// ------------------------------------------------------------------ expectations
struct Topo {
  std::vector<int> chi, shells;  // allowed Euler characteristics / shell counts (empty = not asserted)
};
inline Topo T(int chi, int shells) { return {{chi}, {shells}}; }
inline Topo TA(std::vector<int> chi, std::vector<int> shells) { return {std::move(chi), std::move(shells)}; }
inline Topo TNone() { return {}; }

struct Ex {
  enum Empty { No, Yes, Either } empty = No;
  double vol = NAN, area = NAN;
  bool has_cen = false;
  V3 cen;
  Topo topo;
  bool valid = true, solid = true, tess = true;
  double vol_rel = 1e-3, vol_abs = 0, area_rel = 2e-3, cen_abs = 0;
  std::string oracle;
};

inline J ints(const std::vector<int>& v) {
  if (v.size() == 1) return J(v[0]);
  J a = J::Array();
  for (int x : v) a.push(J(x));
  return a;
}
inline J to_json(const Ex& e) {
  J j = J::Object();
  j.set("empty", e.empty == Ex::No ? J(false) : e.empty == Ex::Yes ? J(true) : J("either"));
  if (!std::isnan(e.vol)) j.set("volume", e.vol);
  if (!std::isnan(e.area)) j.set("area", e.area);
  if (e.has_cen) j.set("centroid", jv(e.cen));
  if (e.empty != Ex::Yes) {
    if (e.valid) j.set("valid", true);
    if (e.solid) j.set("solid", true);
    if (e.tess) j.set("tess_closed", true);
    if (!e.topo.chi.empty()) j.set("euler", ints(e.topo.chi));
    if (!e.topo.shells.empty()) j.set("shells", ints(e.topo.shells));
  }
  j.set("tol", J::O({{"volume_rel", e.vol_rel}, {"volume_abs", e.vol_abs}, {"area_rel", e.area_rel}, {"centroid_abs", e.cen_abs}}));
  if (!e.oracle.empty()) j.set("oracle", e.oracle);
  return j;
}

// Tolerance policy.  Planar-only results are exact (1e-8).  A result with a curved face is
// judged against a GROSS-CORRECTNESS gate of 2e-2 relative on volume and area: the kernel's
// own exact integrals polygonize circular trims (measured on single primitives with no boolean:
// cylinder 3.8e-3, cone 1.1e-2 low), so a tighter gate would score the integrator, not the
// boolean.  The runner records the measured relative error of every case, so the precision
// ladder (1e-2, 1e-3, 1e-4, 1e-6) is tabulated from the same run.  Centroids: absolute,
// relative to the result's size L.  vol_abs floors volumes below 1e-9 of the bounding volume
// (numerically zero).  `noise` is the double-precision noise floor of a divergence-theorem sum
// about the world origin: ~ 16 eps (D / L) with D the far offset.
inline void apply_policy(Ex& e, bool planar, double L, double far_offset = 0) {
  e.vol_rel = planar ? 1e-8 : 2e-2;
  e.area_rel = planar ? 1e-8 : 2e-2;
  e.cen_abs = (planar ? 1e-6 : 5e-3) * L;
  e.vol_abs = 1e-9 * L * L * L;
  if (far_offset > 0) {
    const double noise = 16 * 2.220446e-16 * (far_offset / L) * 10;
    e.vol_rel = std::fmax(e.vol_rel, noise);
    e.area_rel = std::fmax(e.area_rel, noise);
  }
}

// ------------------------------------------------------------------ cases
struct CaseOut {
  std::string id, set, category, descr;
  J operands = J::Array();
  J op = J::Object();
  Ex ex;
  double timeout = 30;
  std::vector<std::string> tags;
};
inline J to_json(const CaseOut& c) {
  J tags = J::Array();
  for (const auto& t : c.tags) tags.push(J(t));
  J j = J::Object();
  j.set("id", c.id).set("set", c.set).set("category", c.category).set("description", c.descr);
  j.set("tags", tags).set("operands", c.operands).set("operation", c.op).set("expect", to_json(c.ex)).set("timeout_s", c.timeout);
  return j;
}
inline J step(const char* op, std::initializer_list<const char*> a, std::initializer_list<const char*> b, const char* out = "R") {
  J ja = J::Array(), jb = J::Array();
  for (const char* x : a) ja.push(J(x));
  for (const char* x : b) jb.push(J(x));
  return J::O({{"op", op}, {"a", ja}, {"b", jb}, {"out", out}});
}
inline J boolean_op(std::initializer_list<J> steps) {
  J st = J::Array();
  for (const J& s : steps) st.push(s);
  return J::O({{"type", "boolean"}, {"engine", "auto"}, {"steps", st}});
}
inline J measure_op(const char* operand) { return J::O({{"type", "measure"}, {"operand", operand}}); }

// A configuration of two operands with every analytic quantity needed to derive the
// expectation of Union, Intersection, A-B and B-A (inclusion-exclusion on volume, area
// and first moment).  All numbers are in final (scaled) units.
struct Pair {
  std::string stem, category, descr, oracle;
  J a, b;
  double Va = 0, Vb = 0, Vi = 0;
  V3 cA, cB, cI;
  bool has_c = true;
  double Sa = 0, Sb = 0, SaIn = 0, SbIn = 0;  // surface areas; part of A's surface inside B and of B's inside A
  bool has_s = true;
  double SU = NAN, SI = NAN, SAB = NAN, SBA = NAN;  // explicit surface areas of the four results (override the inclusion-exclusion)
  Topo tU, tI, tAB, tBA;
  bool planar = false;
  double L = 1;       // characteristic length (bounding diagonal)
  double far = 0;     // far-origin offset (noise floor)
};

// Per-case overrides for evil cases whose exact topology is ambiguous below the
// model tolerance, or non-manifold.
struct Ov {
  bool topo = false;
  Topo t;
  Ex::Empty empty = Ex::No;
  bool force_empty = false;
  bool nostruct = false;  // do not assert valid/solid/tess_closed (non-manifold or ambiguous result)
  bool nocen = false, noarea = false;
  double timeout = 0;
};

inline CaseOut emit(const Pair& p, const std::string& which, const std::string& set, const std::string& idbase, const std::string& note,
                    const Ov& ov = {}) {
  CaseOut c;
  c.set = set;
  c.category = p.category;
  c.id = std::string(set == "golden" ? "G-" : "E-") + idbase + "-" + which;
  c.operands.push(p.a).push(p.b);
  Ex e;
  double V = 0, S = 0;
  V3 cen;
  Topo topo;
  std::string opname;
  if (which == "U") {
    V = p.Va + p.Vb - p.Vi;
    S = (p.Sa - p.SaIn) + (p.Sb - p.SbIn);
    cen = (p.cA * p.Va + p.cB * p.Vb - p.cI * p.Vi) / V;
    topo = p.tU;
    c.op = boolean_op({step("union", {"A"}, {"B"})});
    opname = "Union A+B";
  } else if (which == "I") {
    V = p.Vi;
    S = p.SaIn + p.SbIn;
    cen = p.cI;
    topo = p.tI;
    c.op = boolean_op({step("intersection", {"A"}, {"B"})});
    opname = "Intersection A*B";
  } else if (which == "AB") {
    V = p.Va - p.Vi;
    S = (p.Sa - p.SaIn) + p.SbIn;
    cen = V > 0 ? (p.cA * p.Va - p.cI * p.Vi) / V : p.cA;
    topo = p.tAB;
    c.op = boolean_op({step("difference", {"A"}, {"B"})});
    opname = "Difference A-B";
  } else {
    V = p.Vb - p.Vi;
    S = (p.Sb - p.SbIn) + p.SaIn;
    cen = V > 0 ? (p.cB * p.Vb - p.cI * p.Vi) / V : p.cB;
    topo = p.tBA;
    c.op = boolean_op({step("difference", {"B"}, {"A"})});
    opname = "Difference B-A";
  }
  const bool empty = V <= 0 || (which == "I" && p.Vi <= 0);
  apply_policy(e, p.planar, p.L, p.far);
  e.oracle = p.oracle + (note.empty() ? "" : "; " + note);
  if (empty) {
    e.empty = Ex::Yes;
    e.vol = 0;
  } else {
    e.empty = ov.force_empty ? Ex::Yes : ov.empty;
    e.vol = V;
    const double so = which == "U" ? p.SU : which == "I" ? p.SI : which == "AB" ? p.SAB : p.SBA;
    if (!std::isnan(so)) S = so;
    if ((p.has_s || !std::isnan(so)) && !ov.noarea) e.area = S;
    if (p.has_c && !ov.nocen && p.far == 0) { e.has_cen = true; e.cen = cen; }
    e.topo = ov.topo ? ov.t : topo;
    if (ov.nostruct) e.valid = e.solid = e.tess = false;
  }
  c.ex = e;
  if (ov.timeout > 0) c.timeout = ov.timeout;
  c.descr = opname + ": " + p.descr;
  c.tags = {p.stem};
  return c;
}

inline void add(std::vector<J>& out, const Pair& p, std::initializer_list<const char*> ops, const std::string& set, const std::string& idbase,
                const std::string& note = "", const Ov& ov = {}) {
  for (const char* w : ops) out.push_back(to_json(emit(p, w, set, idbase, note, ov)));
}

}  // namespace corpus
