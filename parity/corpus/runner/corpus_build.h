// Assembles the corpus: golden and evil case lists (see corpus_golden.h, corpus_evil_a.h, corpus_evil_b.h).
#pragma once
#include "corpus_evil_b.h"

namespace corpus {

inline J stepv(const char* op, const std::vector<std::string>& a, const std::vector<std::string>& b, const char* out = "R") {
  J ja = J::Array(), jb = J::Array();
  for (const auto& x : a) ja.push(J(x));
  for (const auto& x : b) jb.push(J(x));
  return J::O({{"op", op}, {"a", ja}, {"b", jb}, {"out", out}});
}

// A measure-only control: one operand, no boolean.  It validates the construction and the
// measurement pipeline themselves against closed forms.
inline J control(const std::string& id, const std::string& descr, const J& operand, double vol, double area, V3 cen, int chi, bool planar, double L) {
  CaseOut c;
  c.id = "G-" + id; c.set = "golden"; c.category = "control"; c.descr = "control (no boolean): " + descr;
  c.operands.push(operand);
  c.op = measure_op("A");
  c.ex.vol = vol; c.ex.area = area; c.ex.has_cen = true; c.ex.cen = cen;
  c.ex.topo = T(chi, 1);
  apply_policy(c.ex, planar, L);
  c.ex.oracle = "closed form of the primitive";
  c.tags = {"control"};
  return to_json(c);
}

inline CaseOut multi(const std::string& id, const std::string& set, const std::string& category, const std::string& descr, const std::string& oracle,
                     const std::vector<J>& operands, const J& op, double vol, double area, V3 cen, Topo topo, bool planar, double L, double timeout = 30,
                     bool has_area = true) {
  CaseOut c;
  c.id = id; c.set = set; c.category = category; c.descr = descr;
  for (const J& o : operands) c.operands.push(o);
  c.op = op;
  c.ex.vol = vol; if (has_area) c.ex.area = area; c.ex.has_cen = true; c.ex.cen = cen; c.ex.topo = topo;
  apply_policy(c.ex, planar, L);
  c.ex.oracle = oracle;
  c.timeout = timeout;
  c.tags = {category};
  return c;
}

inline J plate_grid(int N) {
  const double r = 0.3, t = 0.5;
  std::vector<J> ops;
  ops.push_back(op_box("A", {0, 0, 0}, {double(N), double(N), t}));
  std::vector<std::string> ids;
  for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) {
      char id[24];
      std::snprintf(id, sizeof id, "C%d", j * N + i);
      ops.push_back(op_cyl(id, {i + 0.5, j + 0.5, -0.5}, {0, 0, 1}, r, 1.5));
      ids.push_back(id);
    }
  const double n2 = double(N) * N;
  const double V = n2 * t * (1 - PI * r * r);
  const double S = 2 * n2 + 2 * N + n2 * (2 * PI * r * t - 2 * PI * r * r);
  char id[48], d[256];
  std::snprintf(id, sizeof id, "E-HOLES-%dx%d-AB", N, N);
  std::snprintf(d, sizeof d, "plate %dx%dx0.5 minus %d through cylinders r=0.3 on a unit grid (N-ary difference)", N, N, N * N);
  return to_json(multi(id, "evil", "many-holes", d, "V = N^2 t (1 - pi r^2); genus N^2 so chi = 2 - 2 N^2; area = 2N^2 + 2N + N^2 (2 pi r t - 2 pi r^2)", ops,
                       boolean_op({stepv("difference", {"A"}, ids)}), V, S, {N / 2.0, N / 2.0, t / 2}, T(2 - 2 * N * N, 1), false, std::sqrt(2.0) * N, 30));
}

inline J plate_ring() {
  const double r = 0.55, t = 0.5, lens = cf::circle_lens_area(r, r, 1.0);
  const double U = 4 * PI * r * r - 4 * lens, th = std::acos(1.0 / (2 * r));
  std::vector<J> ops;
  ops.push_back(op_box("A", {0, 0, 0}, {3, 3, t}));
  const double cx[4] = {1, 2, 1, 2}, cy[4] = {1, 1, 2, 2};
  std::vector<std::string> ids;
  for (int k = 0; k < 4; ++k) {
    char id[16];
    std::snprintf(id, sizeof id, "C%d", k);
    ops.push_back(op_cyl(id, {cx[k], cy[k], -0.5}, {0, 0, 1}, r, 1.5));
    ids.push_back(id);
  }
  const double V = t * (9 - U), S = 2 * (9 - U) + 12 * t + 4 * (2 * PI - 4 * th) * r * t;
  return to_json(multi("E-HOLES-RING-AB", "evil", "many-holes",
                       "plate 3x3x0.5 minus four overlapping cylinders r=0.55 centred on the corners of a unit square: their union is a ring that cuts out an island",
                       "union of the disks = 4 pi r^2 - 4 lens(d=1); remaining plate = 9 - union; outer plate (chi 0) + island (chi 2): two shells, chi 2", ops,
                       boolean_op({stepv("difference", {"A"}, ids)}), V, S, {1.5, 1.5, t / 2}, T(2, 2), false, 4.3, 60));
}

inline std::vector<J> nested_cases() {
  std::vector<J> out;
  {  // coaxial through cutters, the inner one nested inside the outer
    const double V = 200 - 8 * PI;
    out.push_back(to_json(multi("E-NEST-COAXIAL-AB", "evil", "nested",
        "plate 10x10x2 minus the group {cylinder r=2 through, cylinder r=1 strictly inside it}: the inner cutter never touches the plate",
        "union of nested cutters = the outer one: V = 200 - pi 2^2 2; hole wall adds 8 pi, the two faces lose 8 pi: area 280",
        {op_box("A", {0, 0, 0}, {10, 10, 2}), op_cyl("B1", {5, 5, -1}, {0, 0, 1}, 2, 4), op_cyl("B2", {5, 5, -0.5}, {0, 0, 1}, 1, 3)},
        boolean_op({stepv("difference", {"A"}, {"B1", "B2"})}), V, 280, {5, 5, 1}, T(0, 1), false, 11)));
  }
  {  // matryoshka boxes
    out.push_back(to_json(multi("E-NEST-MATRYOSHKA-BOX", "evil", "nested",
        "cube [0,10]^3 minus the cube [2,8]^3, then united with the cube [4,6]^3 floating inside the cavity",
        "V = 1000 - 216 + 8 = 792; three disjoint shells (chi 6); area 600 + 216 + 24 = 840",
        {op_box("A", {0, 0, 0}, {10, 10, 10}), op_box("B", {2, 2, 2}, {8, 8, 8}), op_box("C", {4, 4, 4}, {6, 6, 6})},
        boolean_op({stepv("difference", {"A"}, {"B"}, "R1"), stepv("union", {"R1"}, {"C"}, "R")}), 792, 840, {5, 5, 5}, T(6, 3), true, 17.3)));
  }
  {  // nested spheres
    out.push_back(to_json(multi("E-NEST-MATRYOSHKA-SPH", "evil", "nested",
        "sphere R=3 minus the concentric sphere r=2, then united with the concentric sphere r=1 floating in the cavity",
        "V = 4/3 pi (27 - 8 + 1); three shells (chi 6); area 4 pi (9 + 4 + 1) = 56 pi",
        {op_sphere("A", {0, 0, 0}, 3, gen_axis1()), op_sphere("B", {0, 0, 0}, 2, gen_axis2()), op_sphere("C", {0, 0, 0}, 1, {0, 0, 1})},
        boolean_op({stepv("difference", {"A"}, {"B"}, "R1"), stepv("union", {"R1"}, {"C"}, "R")}), 80 * PI / 3, 56 * PI, {0, 0, 0}, T(6, 3), false, 10.4)));
  }
  {  // counterbore from two coaxial overlapping cutters
    const double V = 200 - 5 * PI, Mz = 200 * 1.0 - (4 * PI * 1.5 + PI * 0.5);
    out.push_back(to_json(multi("E-NEST-COUNTERBORE-AB", "evil", "nested",
        "plate [-5,5]^2 x [0,2] minus the stacked coaxial cutters {bore r=2 over z in [1,3], through hole r=1 over z in [-1,3]}",
        "removed = bore 4 pi (z in [1,2]) + hole pi (z in [0,1]); area = 280 + 4 pi (bore wall 4 pi + floor 3 pi + hole wall 2 pi - openings 5 pi)",
        {op_box("A", {-5, -5, 0}, {5, 5, 2}), op_cyl("B1", {0, 0, 1}, {0, 0, 1}, 2, 2), op_cyl("B2", {0, 0, -1}, {0, 0, 1}, 1, 4)},
        boolean_op({stepv("difference", {"A"}, {"B1", "B2"})}), V, 280 + 4 * PI, {0, 0, Mz / V}, T(0, 1), false, 10.5)));
  }
  {  // slot from three overlapping cutters in a row
    const double lens = cf::circle_lens_area(1, 1, 1), U = 3 * PI - 2 * lens, V = 100 - U;
    out.push_back(to_json(multi("E-NEST-SLOT-CHAIN-AB", "evil", "nested",
        "plate [-5,5]^2 x [0,1] minus a chain of three overlapping cylinders r=1 at x = -1, 0, 1",
        "union of the three disks = 3 pi - 2 lens(d=1); slot perimeter 10 pi/3; one through slot (chi 0)",
        {op_box("A", {-5, -5, 0}, {5, 5, 1}), op_cyl("B1", {-1, 0, -1}, {0, 0, 1}, 1, 3), op_cyl("B2", {0, 0, -1}, {0, 0, 1}, 1, 3), op_cyl("B3", {1, 0, -1}, {0, 0, 1}, 1, 3)},
        boolean_op({stepv("difference", {"A"}, {"B1", "B2", "B3"})}), V, 2 * V + 40 + 10 * PI / 3, {0, 0, 0.5}, T(0, 1), false, 10.1)));
  }
  return out;
}

inline std::vector<J> controls() {
  std::vector<J> out;
  out.push_back(control("CTRL-BOX", "box [1.5,4.5]x[-2,2]x[0.5,5.5]", op_box("A", {1.5, -2, 0.5}, {4.5, 2, 5.5}), 60, 94, {3, 0, 3}, 2, true, 7.1));
  {
    const V3 ax = cf::unit({1, 2, 2});
    out.push_back(control("CTRL-CYLINDER", "oblique cylinder r=1.5, h=4 from (1,1,0) along (1,2,2)/3", op_cyl("A", {1, 1, 0}, ax, 1.5, 4), cf::cyl_vol(1.5, 4), cf::cyl_area(1.5, 4),
                          V3{1, 1, 0} + ax * 2.0, 2, false, 5.2));
  }
  out.push_back(control("CTRL-SPHERE", "sphere r=2 at (1,2,3), generic pole axis", op_sphere("A", {1, 2, 3}, 2, gen_axis1()), cf::sph_vol(2), cf::sph_area(2), {1, 2, 3}, 2, false, 4));
  out.push_back(control("CTRL-CONE", "cone with apex at the origin, axis +z, height 3, base radius 1.5", op_cone("A", {0, 0, 0}, {0, 0, 1}, 3, 1.5), cf::cone_vol(1.5, 3), cf::cone_area(1.5, 3), {0, 0, 2.25}, 2, false, 3.8));
  out.push_back(control("CTRL-TORUS", "torus R=3, r=1 about z", op_torus("A", {0, 0, 0}, {0, 0, 1}, 3, 1), cf::torus_vol(3, 1), cf::torus_area(3, 1), {0, 0, 0}, 0, false, 8));
  out.push_back(control("CTRL-PRISM-L", "L-shaped prism: polygon (0,0),(4,0),(4,2),(2,2),(2,4),(0,4), z in [0,3]",
                        op_prism("A", {{0, 0}, {4, 0}, {4, 2}, {2, 2}, {2, 4}, {0, 4}}, 0, 3), 36, 72, {20.0 / 12, 20.0 / 12, 1.5}, 2, true, 6.4));
  return out;
}

inline std::vector<J> BuildCorpus() {
  using namespace golden;
  using namespace evil;
  std::vector<J> out;
  const Frame one;

  // ================= GOLDEN =================
  add(out, boxbox_corner(one), {"U", "I", "AB"}, "golden", "BOXBOX-CORNER");
  add(out, boxbox_rot45(one), {"U", "I", "AB", "BA"}, "golden", "BOXBOX-ROT45");
  add(out, box_in_box(one), {"U", "I", "AB", "BA"}, "golden", "BOX-IN-BOX");
  add(out, box_disjoint(one), {"U", "I", "AB"}, "golden", "BOX-DISJOINT");
  add(out, box_cyl_through(one), {"U", "I", "AB", "BA"}, "golden", "BOX-CYL-THRU");
  add(out, box_cyl_blind(one), {"U", "I", "AB", "BA"}, "golden", "BOX-CYL-BLIND");
  add(out, box_cyl_oblique(one), {"U", "I", "AB"}, "golden", "BOX-CYL-OBLIQUE");
  add(out, cyl_cyl_parallel(one), {"U", "I", "AB"}, "golden", "CYL-CYL-PARALLEL");
  add(out, cyl_cyl_perp(one, 0.5, 0, "CYL-CYL-PERP", "unequal radii, axes meeting"), {"U", "I", "AB", "BA"}, "golden", "CYL-CYL-PERP");
  add(out, cyl_cyl_perp(one, 0.5, 0.3, "CYL-CYL-SKEW", "skew axes"), {"I", "AB"}, "golden", "CYL-CYL-SKEW");
  add(out, sph_box_face(one), {"U", "I", "AB"}, "golden", "SPH-BOX-FACE");
  add(out, sph_box_edge(one), {"U", "I", "AB"}, "golden", "SPH-BOX-EDGE");
  add(out, sph_in_box(one), {"U", "I", "AB"}, "golden", "SPH-IN-BOX");
  add(out, sph_sph(one), {"U", "I", "AB"}, "golden", "SPH-SPH");
  add(out, sph_cyl_x(one), {"U", "I", "AB"}, "golden", "SPH-CYL-X");
  add(out, box_cone_slab(one), {"U", "I", "AB"}, "golden", "BOX-CONE-SLAB");
  add(out, torus_box_half(one), {"U", "I", "AB"}, "golden", "TORUS-BOX-HALF");
  add(out, cyl_tpipe(one), {"U", "AB"}, "golden", "CYL-TPIPE");
  {  // plate with four through holes (N-ary difference)
    std::vector<J> ops{op_box("A", {-10, -10, 0}, {10, 10, 2})};
    std::vector<std::string> ids;
    int k = 0;
    for (double x : {-5.0, 5.0})
      for (double y : {-5.0, 5.0}) {
        char id[16];
        std::snprintf(id, sizeof id, "C%d", k++);
        ops.push_back(op_cyl(id, {x, y, -1}, {0, 0, 1}, 1, 4));
        ids.push_back(id);
      }
    out.push_back(to_json(multi("G-PLATE-4HOLES-AB", "golden", "curved", "plate 20x20x2 minus four through cylinders r=1 at (+-5,+-5) (N-ary difference)",
                                "V = 800 - 4 pi r^2 t; genus 4 so chi = -6; area = 960 + 4 (2 pi r t - 2 pi r^2) = 960 + 8 pi", ops,
                                boolean_op({stepv("difference", {"A"}, ids)}), 800 - 8 * PI, 960 + 8 * PI, {0, 0, 1}, T(-6, 1), false, 29)));
  }
  for (const J& c : controls()) out.push_back(c);

  // ================= EVIL =================
  // near-tangent cylinder / box face
  add(out, tan_cyl_box(1e-3), {"U", "I", "AB"}, "evil", "TAN-CYL-BOX-OV1e-3");
  add(out, tan_cyl_box(1e-6), {"U"}, "evil", "TAN-CYL-BOX-OV1e-6", "", amb_union());
  add(out, tan_cyl_box(1e-6), {"I"}, "evil", "TAN-CYL-BOX-OV1e-6", "", amb_inter());
  add(out, tan_cyl_box(1e-9), {"U"}, "evil", "TAN-CYL-BOX-OV1e-9", "", amb_union());
  add(out, tan_cyl_box(1e-9), {"I"}, "evil", "TAN-CYL-BOX-OV1e-9", "", amb_inter());
  add(out, tan_cyl_box(-1e-3), {"U"}, "evil", "TAN-CYL-BOX-GAP1e-3");
  add(out, tan_cyl_box(-1e-9), {"U"}, "evil", "TAN-CYL-BOX-GAP1e-9", "", amb_union());
  // near-tangent sphere / box face (pole contact and generic)
  add(out, tan_sph_box(1e-3, true), {"U", "I"}, "evil", "TAN-SPH-BOX-POLE-OV1e-3");
  add(out, tan_sph_box(1e-6, true), {"U"}, "evil", "TAN-SPH-BOX-POLE-OV1e-6", "", amb_union());
  add(out, tan_sph_box(1e-9, true), {"U"}, "evil", "TAN-SPH-BOX-POLE-OV1e-9", "", amb_union());
  add(out, tan_sph_box(1e-3, false), {"U"}, "evil", "TAN-SPH-BOX-OV1e-3");
  add(out, tan_sph_box(1e-6, false), {"I"}, "evil", "TAN-SPH-BOX-OV1e-6", "", amb_inter());
  add(out, tan_sph_box(-1e-3, false), {"U"}, "evil", "TAN-SPH-BOX-GAP1e-3");
  // near-tangent sphere / sphere
  add(out, tan_sph_sph(1e-3, false), {"U", "I"}, "evil", "TAN-SPH-SPH-OV1e-3");
  add(out, tan_sph_sph(1e-6, false), {"U"}, "evil", "TAN-SPH-SPH-OV1e-6", "", amb_union());
  add(out, tan_sph_sph(1e-3, true), {"U", "I"}, "evil", "TAN-SPH-SPH-POLE-OV1e-3");
  add(out, tan_sph_sph(-1e-3, false), {"U"}, "evil", "TAN-SPH-SPH-GAP1e-3");
  // near-tangent parallel cylinders
  add(out, tan_cyl_cyl(1e-3), {"U", "I"}, "evil", "TAN-CYL-CYL-OV1e-3");
  add(out, tan_cyl_cyl(1e-6), {"U"}, "evil", "TAN-CYL-CYL-OV1e-6", "", amb_union());
  add(out, tan_cyl_cyl(-1e-3), {"U"}, "evil", "TAN-CYL-CYL-GAP1e-3");
  // near-tangent skew perpendicular cylinders
  add(out, tan_skew(1e-3), {"U", "I"}, "evil", "TAN-SKEW-OV1e-3");
  add(out, tan_skew(1e-6), {"U"}, "evil", "TAN-SKEW-OV1e-6", "", amb_union());
  add(out, tan_skew(1e-6), {"I"}, "evil", "TAN-SKEW-OV1e-6", "", amb_inter());
  add(out, tan_skew(1e-9), {"I"}, "evil", "TAN-SKEW-OV1e-9", "", amb_inter());
  // internal tangency of spheres
  add(out, tan_sph_int(-1e-3), {"AB"}, "evil", "TAN-SPH-INT-IN1e-3");
  { Ov o; o.topo = true; o.t = TA({4, 2}, {2, 1}); add(out, tan_sph_int(-1e-6), {"AB"}, "evil", "TAN-SPH-INT-IN1e-6", "", o); }
  add(out, tan_sph_int(1e-3), {"AB", "BA"}, "evil", "TAN-SPH-INT-OUT1e-3");
  // inscribed bodies
  add(out, inscribed_cyl(), {"U", "I", "BA"}, "evil", "INSCRIBED-CYL");
  { Ov o; o.nostruct = true; add(out, inscribed_cyl(), {"AB"}, "evil", "INSCRIBED-CYL", "", o); }
  add(out, inscribed_sph(), {"U", "I"}, "evil", "INSCRIBED-SPH");
  { Ov o; o.nostruct = true; add(out, inscribed_sph(), {"AB"}, "evil", "INSCRIBED-SPH", "", o); }
  // coincident / near-coincident faces: boss on a block, ladder 1e-3 .. 1e-9
  add(out, coin_boss(1e-3), {"U", "I", "AB"}, "evil", "COIN-BOSS-OV1e-3");
  add(out, coin_boss(1e-6), {"U"}, "evil", "COIN-BOSS-OV1e-6");
  add(out, coin_boss(1e-6), {"I"}, "evil", "COIN-BOSS-OV1e-6", "", amb_inter());
  add(out, coin_boss(1e-9), {"U"}, "evil", "COIN-BOSS-OV1e-9");
  add(out, coin_boss(1e-9), {"I"}, "evil", "COIN-BOSS-OV1e-9", "", amb_inter());
  add(out, coin_boss(-1e-3), {"U"}, "evil", "COIN-BOSS-GAP1e-3");
  add(out, coin_boss(-1e-9), {"U"}, "evil", "COIN-BOSS-GAP1e-9", "", amb_union());
  add(out, coin_boss(0), {"U", "I", "AB"}, "evil", "COIN-BOSS-EXACT");
  // flush stacking with coplanar side faces
  add(out, coin_lunion(0), {"U", "I", "AB", "BA"}, "evil", "COIN-LUNION-FLUSH");
  add(out, coin_lunion(1e-3), {"U"}, "evil", "COIN-LUNION-LIFT1e-3");
  add(out, coin_lunion(1e-6), {"U"}, "evil", "COIN-LUNION-LIFT1e-6");
  // cylinder standing on a box
  add(out, coin_cyl_box(0), {"U"}, "evil", "COIN-CYL-BOX-EXACT");
  add(out, coin_cyl_box(1e-3), {"U", "I"}, "evil", "COIN-CYL-BOX-OV1e-3");
  add(out, coin_cyl_box(1e-9), {"U"}, "evil", "COIN-CYL-BOX-OV1e-9");
  add(out, coin_cyl_box(-1e-3), {"U"}, "evil", "COIN-CYL-BOX-GAP1e-3");
  // coaxial cylinders and coincident spheres
  add(out, coin_coaxial(0), {"U", "I", "AB"}, "evil", "COIN-COAXIAL-EXACT");
  add(out, coin_coaxial(1e-6), {"U", "AB"}, "evil", "COIN-COAXIAL-R1e-6");
  add(out, coin_sphere(0), {"U", "I", "AB"}, "evil", "COIN-SPHERE-EXACT");
  add(out, coin_sphere(1e-6), {"U", "BA"}, "evil", "COIN-SPHERE-R1e-6");
  // scale ladder (default engine tolerances are NOT rescaled: that is the point)
  for (double s : {1e-4, 1e8}) {
    Frame f; f.s = s;
    const std::string sc = s < 1 ? "TINY" : "HUGE";
    add(out, box_cyl_through(f), {"AB"}, "evil", "SCALE-" + sc + "-BOX-CYL-THRU", sc == "TINY" ? "scale 1e-4" : "scale 1e8");
    add(out, sph_box_face(f), {"U"}, "evil", "SCALE-" + sc + "-SPH-BOX-FACE", sc == "TINY" ? "scale 1e-4" : "scale 1e8");
    add(out, cyl_cyl_perp(f, 0.5, 0, "CYL-CYL-PERP", "unequal radii, axes meeting"), {"I"}, "evil", "SCALE-" + sc + "-CYL-CYL-PERP", sc == "TINY" ? "scale 1e-4" : "scale 1e8");
    add(out, boxbox_corner(f), {"U"}, "evil", "SCALE-" + sc + "-BOXBOX-CORNER", sc == "TINY" ? "scale 1e-4" : "scale 1e8");
  }
  // far-from-origin ladder (integer offsets are exact doubles)
  for (double D : {1e6, 1e8, 1e9}) {
    Frame f; f.t = {D, D, D};
    const std::string dt = D == 1e6 ? "1e6" : D == 1e8 ? "1e8" : "1e9";
    add(out, box_cyl_through(f), {"AB"}, "evil", "FAR-" + dt + "-BOX-CYL-THRU", "translated by (" + dt + ")^3");
    if (D != 1e8) add(out, sph_box_face(f), {"U"}, "evil", "FAR-" + dt + "-SPH-BOX-FACE", "translated by (" + dt + ")^3");
    if (D == 1e9) add(out, boxbox_corner(f), {"U"}, "evil", "FAR-" + dt + "-BOXBOX-CORNER", "translated by (" + dt + ")^3");
  }
  // slivers
  add(out, thin_plate(), {"AB", "I"}, "evil", "SLIVER-PLATE");
  add(out, needle(), {"AB", "I"}, "evil", "SLIVER-NEEDLE");
  add(out, wedge(), {"U", "I", "AB", "BA"}, "evil", "SLIVER-WEDGE");
  add(out, bar_sphere(), {"AB", "I"}, "evil", "SLIVER-BAR");
  // near-degenerate edges and vertices
  { Ov o; o.nostruct = true; add(out, edge_contact(0), {"U"}, "evil", "EDGE-CONTACT-EXACT", "", o); }
  add(out, edge_contact(0), {"I"}, "evil", "EDGE-CONTACT-EXACT");
  add(out, edge_contact(1e-3), {"U", "I"}, "evil", "EDGE-CONTACT-OV1e-3");
  { Ov o; o.nostruct = true; add(out, edge_contact(1e-6), {"U"}, "evil", "EDGE-CONTACT-OV1e-6", "", o); }
  { Ov o; o.nostruct = true; add(out, vertex_contact(), {"U"}, "evil", "VERTEX-CONTACT", "", o); }
  add(out, vertex_contact(), {"I"}, "evil", "VERTEX-CONTACT");
  { Ov o; o.nostruct = true; add(out, edge_on_face(0), {"U"}, "evil", "EDGE-ON-FACE-EXACT", "", o); }
  add(out, edge_on_face(0), {"I"}, "evil", "EDGE-ON-FACE-EXACT");
  add(out, edge_on_face(1e-3), {"U", "I"}, "evil", "EDGE-ON-FACE-SUNK1e-3");
  // many-hole plates and ring of overlapping cutters
  for (int N : {4, 5, 8, 10}) out.push_back(plate_grid(N));
  out.push_back(plate_ring());
  // nested cutters
  for (const J& c : nested_cases()) out.push_back(c);
  // periodic seams
  add(out, seam_cyl_half(), {"U", "I", "AB"}, "evil", "SEAM-CYL-HALF");
  add(out, seam_sph_half(), {"I", "AB"}, "evil", "SEAM-SPH-HALF");
  add(out, seam_torus_half(), {"I", "BA"}, "evil", "SEAM-TORUS-HALF");
  add(out, seam_hole(), {"U", "I", "AB"}, "evil", "SEAM-HOLE");
  // poles and apexes
  add(out, pole_cyl(), {"U", "I", "AB"}, "evil", "POLE-CYL");
  { Ov o; o.nostruct = true; add(out, apex_box(0), {"U"}, "evil", "APEX-BOX-EXACT", "", o); }
  add(out, apex_box(0), {"I"}, "evil", "APEX-BOX-EXACT");
  add(out, apex_box(1e-3), {"U"}, "evil", "APEX-BOX-SUNK1e-3");
  add(out, apex_box(1e-3), {"I"}, "evil", "APEX-BOX-SUNK1e-3", "", amb_inter());
  add(out, apex_box(1e-6), {"U"}, "evil", "APEX-BOX-SUNK1e-6", "", amb_union());
  add(out, apex_cyl(), {"U", "I", "AB", "BA"}, "evil", "APEX-CYL");
  add(out, apex_pole(), {"U", "I"}, "evil", "APEX-POLE");
  { Ov o; o.nostruct = true; add(out, apex_pole(), {"AB"}, "evil", "APEX-POLE", "", o); }
  // singular crossing and size ratio
  add(out, steinmetz(), {"U", "I"}, "evil", "STEINMETZ");
  { Ov o; o.nostruct = true; add(out, steinmetz(), {"AB"}, "evil", "STEINMETZ", "", o); }
  add(out, ratio_hole(), {"AB"}, "evil", "RATIO-HOLE");
  add(out, ratio_boss(), {"U"}, "evil", "RATIO-BOSS");
  return out;
}

}  // namespace corpus
