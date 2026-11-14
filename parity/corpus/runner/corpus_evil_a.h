// Evil configurations, part A: near-tangent pairs, internal tangency, inscribed bodies and
// coincident / near-coincident faces.  `delta` is the penetration depth (> 0) or the gap
// (< 0) in model units; |delta| <= 1e-6 is inside the kernel's model tolerance
// (tolerance::kDistance), where a merged and a separated result are both acceptable
// topologies (expectations then list both).
#pragma once
#include "corpus_golden.h"

namespace corpus {
namespace evil {

using cf::cyl_area;
using cf::cyl_vol;
using cf::sph_area;
using cf::sph_vol;

inline std::string tag(double d) {
  const double a = std::fabs(d);
  if (a == 0) return "0";
  char b[48];
  std::snprintf(b, sizeof b, "1e-%d", static_cast<int>(std::lround(-std::log10(a))));
  return b;
}
inline bool within_tol(double d) { return std::fabs(d) <= 1e-6; }

// Overrides for a result whose topology is merged-or-separated below the model tolerance.
inline Ov amb_union() { Ov o; o.topo = true; o.t = TA({2, 4}, {1, 2}); return o; }
inline Ov amb_inter() { Ov o; o.empty = Ex::Either; return o; }

// ---- cylinder against a box face: z-axis cylinder r=1 whose wall passes `delta` inside the face x=0 of [0,4]^3
inline Pair tan_cyl_box(double delta) {
  Pair p;
  const double r = 1, cx = -r + delta;
  p.stem = "TAN-CYL-BOX"; p.category = "near-tangent";
  p.descr = std::string("z-axis cylinder r=1 (z in [-1,5]) whose wall is ") + (delta > 0 ? "inside" : "outside") + " the face x=0 of the box [0,4]^3 by " + tag(delta);
  p.oracle = "overlap = circular segment (r=1, depth delta) x 4; segment area from the half-angle series (no cancellation); wall arc 2 r theta";
  p.a = op_box("A", {0, 0, 0}, {4, 4, 4});
  p.b = op_cyl("B", {cx, 2, -1}, {0, 0, 1}, r, 6);
  const double th = delta > 0 ? 2 * std::asin(std::sqrt(delta / (2 * r))) : 0;
  p.Va = 64; p.Vb = 6 * PI; p.Vi = delta > 0 ? 4 * cf::seg_area(r, delta) : 0;
  p.cA = {2, 2, 2}; p.cB = {cx, 2, 2}; p.cI = {delta > 0 ? cx + cf::seg_centroid(r, delta) : 0, 2, 2};
  p.Sa = 96; p.Sb = 14 * PI;
  p.SaIn = delta > 0 ? 4 * 2 * std::sqrt(2 * r * delta - delta * delta) : 0;
  p.SbIn = delta > 0 ? 4 * 2 * r * th : 0;
  p.tU = delta > 0 ? T(2, 1) : T(4, 2); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = std::sqrt(16 + 16 + 36);
  return p;
}

// ---- sphere against the top face z=4 of the box [0,4]^3, centre above (2,2); `pole` puts a pole at the contact
inline Pair tan_sph_box(double delta, bool pole) {
  Pair p;
  const double R = 1, zc = 4 + R - delta;
  p.stem = pole ? "TAN-SPH-BOX-POLE" : "TAN-SPH-BOX"; p.category = "near-tangent";
  p.descr = std::string("sphere r=1 ") + (pole ? "(pole axis z, so a pole meets the face) " : "(generic axis) ") + (delta > 0 ? "sinking" : "hovering") + " " + tag(delta) + " at the face z=4 of the box [0,4]^3";
  p.oracle = "overlap = spherical cap of height delta: pi delta^2 (3R-delta)/3, centroid 3(2R-h)^2/(4(3R-h)) from the centre; A's disk radius^2 = 2 R delta - delta^2";
  p.a = op_box("A", {0, 0, 0}, {4, 4, 4});
  p.b = op_sphere("B", {2, 2, zc}, R, pole ? V3{0, 0, 1} : gen_axis1());
  p.Va = 64; p.Vb = sph_vol(R); p.Vi = delta > 0 ? cf::cap_vol(R, delta) : 0;
  p.cA = {2, 2, 2}; p.cB = {2, 2, zc}; p.cI = {2, 2, zc - cf::cap_centroid(R, delta > 0 ? delta : 0)};
  p.Sa = 96; p.Sb = sph_area(R);
  p.SaIn = delta > 0 ? PI * (2 * R * delta - delta * delta) : 0; p.SbIn = delta > 0 ? cf::cap_area(R, delta) : 0;
  p.tU = delta > 0 ? T(2, 1) : T(4, 2); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = std::sqrt(16 + 16 + 36);
  return p;
}

// ---- two unit spheres whose centres are 2 - delta apart
inline Pair tan_sph_sph(double delta, bool pole) {
  Pair p;
  const double d = 2 - delta;
  p.stem = pole ? "TAN-SPH-SPH-POLE" : "TAN-SPH-SPH"; p.category = "near-tangent";
  p.descr = std::string("unit spheres ") + (pole ? "with both poles on the x axis (pole to pole) " : "with generic axes ") + (delta > 0 ? "overlapping by " : "separated by ") + tag(delta);
  p.oracle = "lens = two caps of height delta/2: 2 pi (delta/2)^2 (3 - delta/2)/3 about x = 1 - delta/2; cap areas pi delta";
  p.a = op_sphere("A", {0, 0, 0}, 1, pole ? V3{1, 0, 0} : gen_axis1());
  p.b = op_sphere("B", {d, 0, 0}, 1, pole ? V3{1, 0, 0} : gen_axis2());
  p.Va = p.Vb = sph_vol(1); p.Vi = delta > 0 ? 2 * cf::cap_vol(1, delta / 2) : 0;
  p.cA = {0, 0, 0}; p.cB = {d, 0, 0}; p.cI = {1 - delta / 2, 0, 0};
  p.Sa = p.Sb = sph_area(1); p.SaIn = p.SbIn = delta > 0 ? PI * delta : 0;
  p.tU = delta > 0 ? T(2, 1) : T(4, 2); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = 4;
  return p;
}

// ---- two parallel z-axis cylinders r=1, axes 2 - delta apart; A z in [0,3], B z in [0.5,3.5]
inline Pair tan_cyl_cyl(double delta) {
  Pair p;
  const double d = 2 - delta, lens = delta > 0 ? 2 * cf::seg_area(1, delta / 2) : 0;
  const double th = delta > 0 ? 2 * std::asin(std::sqrt(delta / 4)) : 0;
  p.stem = "TAN-CYL-CYL"; p.category = "near-tangent";
  p.descr = std::string("parallel z-axis cylinders r=1 (A z in [0,3], B z in [0.5,3.5]) ") + (delta > 0 ? "overlapping by " : "separated by ") + tag(delta);
  p.oracle = "lens area = 2 x segment(r=1, depth delta/2) times axial overlap 2.5; wall arcs 2 theta, cos(theta) = 1 - delta/2";
  p.a = op_cyl("A", {0, 0, 0}, {0, 0, 1}, 1, 3);
  p.b = op_cyl("B", {d, 0, 0.5}, {0, 0, 1}, 1, 3);
  p.Va = p.Vb = 3 * PI; p.Vi = lens * 2.5;
  p.cA = {0, 0, 1.5}; p.cB = {d, 0, 2}; p.cI = {1 - delta / 2, 0, 1.75};
  p.Sa = p.Sb = 8 * PI; p.SaIn = p.SbIn = delta > 0 ? 2 * th * 2.5 + lens : 0;
  p.tU = delta > 0 ? T(2, 1) : T(4, 2); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = std::sqrt(9 + 1 + 16);
  return p;
}

// ---- perpendicular cylinders (z-axis r=1 and y-axis r=0.5) whose axes are 1.5 - delta apart: near-tangent skew crossing
inline Pair tan_skew(double delta) {
  Pair p = golden::cyl_cyl_perp(Frame{}, 0.5, 1.5 - delta, "TAN-SKEW", "near tangent");
  p.category = "near-tangent";
  p.descr = std::string("z-axis cylinder r=1 and a y-axis cylinder r=0.5 whose axis is offset 1.5-") + tag(delta) + " in x (skew, near external tangency)";
  p.has_c = false; p.has_s = false;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  return p;
}

// ---- unit sphere B against the sphere A (R=2): d = 1 + e, e < 0 strictly inside, e > 0 poking out by e
inline Pair tan_sph_int(double e) {
  Pair p;
  const double R = 2, r = 1, d = 1 + e;
  p.stem = "TAN-SPH-INT"; p.category = "near-tangent";
  p.descr = std::string("unit sphere ") + (e < 0 ? "inside" : "poking out of") + " the sphere R=2 by " + tag(e) + " from internal tangency";
  p.oracle = "lens of the two spheres (cap volumes about the radical plane); inside case: B is contained";
  p.a = op_sphere("A", {0, 0, 0}, R, gen_axis1());
  p.b = op_sphere("B", {d, 0, 0}, r, gen_axis2());
  p.Va = sph_vol(R); p.Vb = sph_vol(r);
  p.Sa = sph_area(R); p.Sb = sph_area(r);
  if (e < 0) {
    p.Vi = p.Vb; p.cA = {0, 0, 0}; p.cB = p.cI = {d, 0, 0}; p.SaIn = 0; p.SbIn = p.Sb;
    p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(4, 2); p.tBA = T(2, 1);
  } else {
    p.Vi = cf::sphere_lens_vol(R, r, d);
    const double x0 = (d * d + R * R - r * r) / (2 * d);
    p.SaIn = cf::cap_area(R, R - x0); p.SbIn = cf::cap_area(r, r - (d - x0));
    p.has_c = false;
    p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  }
  p.L = 4.5;
  return p;
}

// ---- cylinder inscribed in a square prism (tangent along 4 lines, one of them on the seam)
inline Pair inscribed_cyl() {
  Pair p;
  p.stem = "INSCRIBED-CYL"; p.category = "tangent";
  p.descr = "z-axis cylinder r=1 (z in [-1,3]) inscribed in the prism [-1,1]^2 x [0,2]: tangent along four lines, one on the cylinder seam";
  p.oracle = "overlap is the cylinder section z in [0,2] (2 pi); the wall inside touches the faces along lines only; A-B leaves four corner pieces meeting only along those lines";
  p.a = op_box("A", {-1, -1, 0}, {1, 1, 2});
  p.b = op_cyl("B", {0, 0, -1}, {0, 0, 1}, 1, 4);
  p.Va = 8; p.Vb = 4 * PI; p.Vi = 2 * PI;
  p.cA = p.cB = p.cI = {0, 0, 1};
  p.Sa = 24; p.Sb = 10 * PI; p.SaIn = 2 * PI; p.SbIn = 4 * PI;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = TNone(); p.tBA = T(4, 2);
  p.L = std::sqrt(4 + 4 + 16);
  return p;
}
inline Pair inscribed_sph() {
  Pair p;
  p.stem = "INSCRIBED-SPH"; p.category = "tangent";
  p.descr = "unit sphere inscribed in the cube [-1,1]^3: tangent at the six face centres";
  p.oracle = "overlap = the sphere; union = the cube; cube minus sphere touches itself at six points";
  p.a = op_box("A", {-1, -1, -1}, {1, 1, 1});
  p.b = op_sphere("B", {0, 0, 0}, 1, gen_axis1());
  p.Va = 8; p.Vb = sph_vol(1); p.Vi = p.Vb;
  p.cA = p.cB = p.cI = {0, 0, 0};
  p.Sa = 24; p.Sb = sph_area(1); p.SaIn = 0; p.SbIn = p.Sb;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = TNone(); p.tBA = T(2, 1);
  p.L = std::sqrt(12.0);
  return p;
}

// ---- a footprint-inside box B (x,y in [0.5,1.5]) meeting the top face z=2 of A=[0,2]^3: B starts at z = 2 - delta
//      (delta > 0 overlap, 0 exact contact, < 0 gap)
inline Pair coin_boss(double delta) {
  Pair p;
  const double zb = 2 - delta, hb = 4 - zb;
  p.stem = "COIN-BOSS"; p.category = "coincident"; p.planar = true;
  p.descr = std::string("box [0,2]^3 and a 1x1 box standing on its top face, starting ") + (delta > 0 ? "inside it by " : delta < 0 ? "above it by " : "exactly on it ") + (delta == 0 ? "" : tag(delta));
  p.oracle = "planar, exact: overlap = 1 x 1 x delta; union is the cube plus a boss of height 2 (volume 10, area 32)";
  p.a = op_box("A", {0, 0, 0}, {2, 2, 2});
  p.b = op_box("B", {0.5, 0.5, zb}, {1.5, 1.5, 4});
  p.Va = 8; p.Vb = hb; p.Vi = delta > 0 ? delta : 0;
  p.cA = {1, 1, 1}; p.cB = {1, 1, (zb + 4) / 2}; p.cI = {1, 1, 2 - delta / 2};
  p.Sa = 24; p.Sb = 2 + 4 * hb;
  if (delta > 0) { p.SaIn = 1; p.SbIn = 1 + 4 * delta; }
  else if (delta == 0) { p.SaIn = 1; p.SbIn = 1; }
  else { p.SaIn = 0; p.SbIn = 0; }
  p.tU = delta >= 0 ? T(2, 1) : T(4, 2); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = std::sqrt(4 + 4 + 16);
  return p;
}

// ---- two flush boxes sharing four coplanar side faces over part of their length
inline Pair coin_lunion(double lift) {
  Pair p;
  p.stem = "COIN-LUNION"; p.category = "coincident"; p.planar = true;
  p.descr = std::string("boxes [0,2]^3 and [1,3]x[0,2]x[") + J::Number(lift) + "," + J::Number(2 + lift) + "] whose side faces y=0 and y=2 are coplanar";
  p.oracle = lift == 0 ? "flush union is the box [0,3]x[0,2]^2 (volume 12, area 32); overlap [1,2]x[0,2]^2" : "stepped union; overlap [1,2]x[0,2]x[lift,2] = 4 - 2 lift";
  p.a = op_box("A", {0, 0, 0}, {2, 2, 2});
  p.b = op_box("B", {1, 0, lift}, {3, 2, 2 + lift});
  p.Va = p.Vb = 8; p.Vi = 2 * (2 - lift);
  p.cA = {1, 1, 1}; p.cB = {2, 1, 1 + lift}; p.cI = {1.5, 1, (lift + 2) / 2};
  p.Sa = p.Sb = 24;
  if (lift == 0) { p.SU = 32; p.SI = 16; p.SAB = 16; p.SBA = 16; }
  else { p.SU = 32 + 6 * lift; p.has_s = false; }
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = std::sqrt(9 + 4 + 4);
  return p;
}

// ---- cylinder r=1 standing on the top face of the box [-2,2]x[-2,2]x[0,2]; base at z = 2 - delta, top at z = 5
inline Pair coin_cyl_box(double delta) {
  Pair p;
  const double zb = 2 - delta, hb = 5 - zb;
  p.stem = "COIN-CYL-BOX"; p.category = "coincident";
  p.descr = std::string("cylinder r=1 whose flat base ") + (delta > 0 ? "sinks " + tag(delta) + " into" : delta < 0 ? "hovers " + tag(delta) + " above" : "lies exactly on") + " the top face z=2 of the box [-2,2]^2 x [0,2]";
  p.oracle = "exact contact: boss on a block (volume 32 + 3 pi, area 64 + 6 pi); overlap = pi delta";
  p.a = op_box("A", {-2, -2, 0}, {2, 2, 2});
  p.b = op_cyl("B", {0, 0, zb}, {0, 0, 1}, 1, hb);
  p.Va = 32; p.Vb = PI * hb; p.Vi = delta > 0 ? PI * delta : 0;
  p.cA = {0, 0, 1}; p.cB = {0, 0, (zb + 5) / 2}; p.cI = {0, 0, 2 - delta / 2};
  p.Sa = 64; p.Sb = 2 * PI + 2 * PI * hb;
  if (delta > 0) { p.SaIn = PI; p.SbIn = PI + 2 * PI * delta; }
  else if (delta == 0) { p.SaIn = PI; p.SbIn = PI; }
  else { p.SaIn = 0; p.SbIn = 0; }
  p.tU = delta >= 0 ? T(2, 1) : T(4, 2); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = std::sqrt(16 + 16 + 25);
  return p;
}

// ---- coaxial cylinders with (nearly) the same radius: A r=1 z in [0,2], B r=1+eps z in [1,3]
inline Pair coin_coaxial(double eps) {
  Pair p;
  const double rb = 1 + eps;
  p.stem = "COIN-COAXIAL"; p.category = "coincident";
  p.descr = eps == 0 ? "two coaxial cylinders r=1 sharing the lateral surface over z in [1,2]" : std::string("coaxial cylinders r=1 and r=1+") + tag(eps) + ", z in [0,2] and [1,3]";
  p.oracle = eps == 0 ? "union is the cylinder z in [0,3] (3 pi); overlap and differences are cylinders of height 1" : "A inside B over z in [1,2]: U = pi + 2 pi rb^2, A-B = pi (z in [0,1])";
  p.a = op_cyl("A", {0, 0, 0}, {0, 0, 1}, 1, 2);
  p.b = op_cyl("B", {0, 0, 1}, {0, 0, 1}, rb, 2);
  p.Va = 2 * PI; p.Vb = 2 * PI * rb * rb; p.Vi = PI;
  p.cA = {0, 0, 1}; p.cB = {0, 0, 2}; p.cI = {0, 0, 1.5};
  p.Sa = 6 * PI; p.Sb = 2 * PI * rb * rb + 4 * PI * rb;
  if (eps == 0) { p.SU = 8 * PI; p.SI = 4 * PI; p.SAB = 4 * PI; p.SBA = 4 * PI; }
  else { p.has_s = false; p.has_c = false; }
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = std::sqrt(4 + 4 + 9);
  return p;
}

// ---- coincident spheres: B is A itself (eps = 0) or concentric with radius 1+eps
inline Pair coin_sphere(double eps) {
  Pair p;
  const double rb = 1 + eps;
  p.stem = "COIN-SPHERE"; p.category = "coincident";
  p.descr = eps == 0 ? "two identical unit spheres (different pole axes)" : std::string("concentric spheres r=1 and r=1+") + tag(eps);
  p.oracle = eps == 0 ? "identical solids: U = I = A, A-B = B-A = empty" : "B contains A: U = B, I = A, A-B empty, B-A = thin shell 4/3 pi ((1+eps)^3 - 1)";
  p.a = op_sphere("A", {0, 0, 0}, 1, gen_axis1());
  p.b = op_sphere("B", {0, 0, 0}, rb, gen_axis2());
  p.Va = sph_vol(1); p.Vb = sph_vol(rb); p.Vi = p.Va;
  p.cA = p.cB = p.cI = {0, 0, 0};
  p.Sa = sph_area(1); p.Sb = sph_area(rb);
  if (eps == 0) { p.SU = p.SI = sph_area(1); p.SAB = p.SBA = 0; }
  else { p.SaIn = p.Sa; p.SbIn = 0; }  // A's surface is inside B, B's is outside A: U = Sb, I = Sa, B-A = Sb + Sa
  p.tU = p.tI = p.tAB = T(2, 1); p.tBA = eps == 0 ? T(2, 1) : T(4, 2);
  p.L = 2 * rb * 1.8;
  return p;
}

}  // namespace evil
}  // namespace corpus
