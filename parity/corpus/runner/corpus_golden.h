// Golden configurations: well-posed pairs in general position (no tangency, no
// coincident faces, no poles or seams on a cutter), each with closed-form oracles.
#pragma once
#include "corpus_cases.h"

namespace corpus {
namespace golden {

using cf::cyl_area;
using cf::cyl_vol;
using cf::sph_area;
using cf::sph_vol;

inline Pair boxbox_corner(const Frame& f) {
  Pair p;
  p.stem = "BOXBOX-CORNER"; p.category = "planar"; p.planar = true;
  p.descr = "two cubes of side 2 overlapping in a unit cube at one corner";
  p.oracle = "overlap is the unit cube [1,2]^3; each operand loses 3 unit faces to the other";
  p.a = op_box("A", f.P(0, 0, 0), f.P(2, 2, 2));
  p.b = op_box("B", f.P(1, 1, 1), f.P(3, 3, 3));
  p.Va = f.V(8); p.Vb = f.V(8); p.Vi = f.V(1);
  p.cA = f.P(1, 1, 1); p.cB = f.P(2, 2, 2); p.cI = f.P(1.5, 1.5, 1.5);
  p.Sa = f.A(24); p.Sb = f.A(24); p.SaIn = f.A(3); p.SbIn = f.A(3);
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(27.0)); p.far = f.far();
  return p;
}

inline Pair boxbox_rot45(const Frame& f) {
  Pair p;
  const double k = std::sqrt(2.0) - 1, O = 8 * k;  // octagon of two concentric squares of side 2, 45 degrees apart
  p.stem = "BOXBOX-ROT45"; p.category = "planar"; p.planar = true;
  p.descr = "square prism [-1,1]^2 x [0,2] against the same prism turned 45 degrees about z and raised to z in [0.5,2.5]";
  p.oracle = "overlap = regular octagon (inradius 1, area 8(sqrt2-1)) x height 1.5; A-B is a pocket with four corner posts";
  p.a = op_box("A", f.P(-1, -1, 0), f.P(1, 1, 2));
  p.b = op_box_rot("B", f.P(-1, -1, 0.5), f.P(1, 1, 2.5), {0, 0, 1}, 45.0, f.P(0, 0, 0));
  p.Va = f.V(8); p.Vb = f.V(8); p.Vi = f.V(1.5 * O);
  p.cA = f.P(0, 0, 1); p.cB = f.P(0, 0, 1.5); p.cI = f.P(0, 0, 1.25);
  p.Sa = f.A(24); p.Sb = f.A(24);
  p.SaIn = f.A(12 * k + O);  // 4 sides x 2k x 1.5 = 12k, plus the top face octagon
  p.SbIn = f.A(12 * k + O);
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(4 + 4 + 6.25)); p.far = f.far();
  return p;
}

inline Pair box_in_box(const Frame& f) {
  Pair p;
  p.stem = "BOX-IN-BOX"; p.category = "planar"; p.planar = true;
  p.descr = "cube [1,2]^3 strictly inside the cube [0,4]^3";
  p.oracle = "containment: union is the big cube, intersection the small one, A-B the big cube with a closed cavity, B-A empty";
  p.a = op_box("A", f.P(0, 0, 0), f.P(4, 4, 4));
  p.b = op_box("B", f.P(1, 1, 1), f.P(2, 2, 2));
  p.Va = f.V(64); p.Vb = f.V(1); p.Vi = f.V(1);
  p.cA = f.P(2, 2, 2); p.cB = f.P(1.5, 1.5, 1.5); p.cI = p.cB;
  p.Sa = f.A(96); p.Sb = f.A(6); p.SaIn = 0; p.SbIn = f.A(6);
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(4, 2); p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(48.0)); p.far = f.far();
  return p;
}

inline Pair box_disjoint(const Frame& f) {
  Pair p;
  p.stem = "BOX-DISJOINT"; p.category = "planar"; p.planar = true;
  p.descr = "cubes [0,2]^3 and [3,5]^3 with a gap of 1";
  p.oracle = "disjoint: union is two shells (volume 16), intersection empty, differences return the operand";
  p.a = op_box("A", f.P(0, 0, 0), f.P(2, 2, 2));
  p.b = op_box("B", f.P(3, 3, 3), f.P(5, 5, 5));
  p.Va = f.V(8); p.Vb = f.V(8); p.Vi = 0;
  p.cA = f.P(1, 1, 1); p.cB = f.P(4, 4, 4); p.cI = f.P(0, 0, 0);
  p.Sa = f.A(24); p.Sb = f.A(24); p.SaIn = p.SbIn = 0;
  p.tU = T(4, 2); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(75.0)); p.far = f.far();
  return p;
}

inline Pair box_cyl_through(const Frame& f) {
  Pair p;
  p.stem = "BOX-CYL-THRU"; p.category = "curved";
  p.descr = "box [-2,2]^2 x [-1,1] pierced by a z-axis cylinder r=1 spanning z in [-2,2]";
  p.oracle = "overlap = pi r^2 x slab 2; A's faces inside B = two disks; B's wall inside A = 2 pi r x 2";
  p.a = op_box("A", f.P(-2, -2, -1), f.P(2, 2, 1));
  p.b = op_cyl("B", f.P(0, 0, -2), {0, 0, 1}, f.L(1), f.L(4));
  p.Va = f.V(32); p.Vb = f.V(4 * PI); p.Vi = f.V(2 * PI);
  p.cA = p.cB = p.cI = f.P(0, 0, 0);
  p.Sa = f.A(64); p.Sb = f.A(10 * PI); p.SaIn = f.A(2 * PI); p.SbIn = f.A(4 * PI);
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = f.L(std::sqrt(16 + 16 + 16)); p.far = f.far();
  return p;
}

inline Pair box_cyl_blind(const Frame& f) {
  Pair p;
  p.stem = "BOX-CYL-BLIND"; p.category = "curved";
  p.descr = "box [-2,2]^2 x [-1,1] and a z-axis cylinder r=1 over z in [0,3] (blind hole / boss)";
  p.oracle = "overlap = pi r^2 x 1 (z in [0,1]); A's top face disk and B's wall + bottom disk lie inside the other";
  p.a = op_box("A", f.P(-2, -2, -1), f.P(2, 2, 1));
  p.b = op_cyl("B", f.P(0, 0, 0), {0, 0, 1}, f.L(1), f.L(3));
  p.Va = f.V(32); p.Vb = f.V(3 * PI); p.Vi = f.V(PI);
  p.cA = f.P(0, 0, 0); p.cB = f.P(0, 0, 1.5); p.cI = f.P(0, 0, 0.5);
  p.Sa = f.A(64); p.Sb = f.A(8 * PI); p.SaIn = f.A(PI); p.SbIn = f.A(3 * PI);
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(16 + 16 + 16)); p.far = f.far();
  return p;
}

inline Pair box_cyl_oblique(const Frame& f) {
  Pair p;
  // axis = Ry(20 deg) Rx(30 deg) z
  double dx = 0, dy = -std::sin(PI / 6), dz = std::cos(PI / 6);
  const double c20 = std::cos(PI / 9), s20 = std::sin(PI / 9);
  const V3 d{dx * c20 + dz * s20, dy, -dx * s20 + dz * c20};
  const double ct = d.z;
  p.stem = "BOX-CYL-OBLIQUE"; p.category = "curved";
  p.descr = "box [-3,3]^2 x [-1,1] pierced by an oblique cylinder r=1, length 12, through the box centre";
  p.oracle = "slab of thickness 2 cut along an axis at angle theta (cos theta = d.z): overlap = pi r^2 x 2/cos(theta); wall inside = 2 pi r x 2/cos(theta)";
  p.a = op_box("A", f.P(-3, -3, -1), f.P(3, 3, 1));
  p.b = op_cyl("B", f.P(V3{0, 0, 0} - d * 6.0), d, f.L(1), f.L(12));
  p.Va = f.V(72); p.Vb = f.V(12 * PI); p.Vi = f.V(2 * PI / ct);
  p.cA = p.cB = p.cI = f.P(0, 0, 0);
  p.Sa = f.A(120); p.Sb = f.A(26 * PI); p.SaIn = f.A(2 * PI / ct); p.SbIn = f.A(4 * PI / ct);
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = f.L(std::sqrt(36 + 36 + 4)); p.far = f.far();
  return p;
}

inline Pair cyl_cyl_parallel(const Frame& f) {
  Pair p;
  const double lens = cf::circle_lens_area(1, 1, 1);
  p.stem = "CYL-CYL-PARALLEL"; p.category = "curved";
  p.descr = "two z-axis cylinders r=1, axes 1 apart, A z in [0,4], B z in [1,5]";
  p.oracle = "overlap = circle-circle lens (r=1, d=1) x axial overlap 3; walls: arc 2 pi/3 x 3";
  p.a = op_cyl("A", f.P(0, 0, 0), {0, 0, 1}, f.L(1), f.L(4));
  p.b = op_cyl("B", f.P(1, 0, 1), {0, 0, 1}, f.L(1), f.L(4));
  p.Va = p.Vb = f.V(4 * PI); p.Vi = f.V(3 * lens);
  p.cA = f.P(0, 0, 2); p.cB = f.P(1, 0, 3); p.cI = f.P(0.5, 0, 2.5);
  p.Sa = p.Sb = f.A(10 * PI); p.SaIn = p.SbIn = f.A(lens + 2 * PI);
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(4 + 1 + 25)); p.far = f.far();
  return p;
}

// Perpendicular cylinders: A along z (radius 1, z in [-3,3]); B along y (radius rb, y in [-3,3]) with its
// axis offset by `off` along x.  `off` = 0 is the unequal-radius crossing, off > 0 the skew crossing.
inline Pair cyl_cyl_perp(const Frame& f, double rb, double off, const char* stem, const char* what) {
  Pair p;
  const double ra = 1;
  const double Vi = cf::perp_cyl_vol(ra, rb, off), My = cf::perp_cyl_moment_y(ra, rb, off);
  p.stem = stem; p.category = "curved";
  p.descr = std::string("z-axis cylinder r=1 (z in [-3,3]) crossed by a y-axis cylinder r=") + J::Number(rb) + ", axis offset " + J::Number(off) + " (" + what + ")";
  p.oracle = "overlap = integral 4 sqrt(ra^2-x^2) sqrt(rb^2-(x-off)^2) dx (Gauss-Legendre, 1e-14); walls by quadrature of the wall-in-cylinder area";
  p.a = op_cyl("A", f.P(0, 0, -3), {0, 0, 1}, f.L(ra), f.L(6));
  p.b = op_cyl("B", f.P(off, -3, 0), {0, 1, 0}, f.L(rb), f.L(6));
  p.Va = f.V(cyl_vol(ra, 6)); p.Vb = f.V(cyl_vol(rb, 6)); p.Vi = f.V(Vi);
  p.cA = f.P(0, 0, 0); p.cB = f.P(off, 0, 0); p.cI = f.P(My / Vi, 0, 0);
  p.Sa = f.A(cyl_area(ra, 6)); p.Sb = f.A(cyl_area(rb, 6));
  p.SaIn = f.A(cf::perp_cyl_wallA_in_B(ra, rb, off)); p.SbIn = f.A(cf::perp_cyl_wallB_in_A(ra, rb, off));
  p.tU = p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = f.L(std::sqrt(36 + 4 + 4)); p.far = f.far();
  return p;
}

inline Pair sph_box_face(const Frame& f) {
  Pair p;
  p.stem = "SPH-BOX-FACE"; p.category = "curved";
  p.descr = "sphere r=2 centred on the face x=0 of the box [0,10]x[-5,5]^2 (generic pole axis)";
  p.oracle = "overlap = half sphere (2/3 pi r^3), centroid 3r/8 from the plane; A's face inside B = disk pi r^2; B's surface inside A = 2 pi r^2";
  p.a = op_box("A", f.P(0, -5, -5), f.P(10, 5, 5));
  p.b = op_sphere("B", f.P(0, 0, 0), f.L(2), gen_axis1());
  p.Va = f.V(1000); p.Vb = f.V(sph_vol(2)); p.Vi = f.V(sph_vol(2) / 2);
  p.cA = f.P(5, 0, 0); p.cB = f.P(0, 0, 0); p.cI = f.P(3.0 * 2 / 8, 0, 0);
  p.Sa = f.A(600); p.Sb = f.A(sph_area(2)); p.SaIn = f.A(PI * 4); p.SbIn = f.A(sph_area(2) / 2);
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(100 + 100 + 100)); p.far = f.far();
  return p;
}

inline Pair sph_box_edge(const Frame& f) {
  Pair p;
  p.stem = "SPH-BOX-EDGE"; p.category = "curved";
  p.descr = "sphere r=2 centred on the edge x=y=0 of the box [0,10]^2 x [-5,5] (generic pole axis)";
  p.oracle = "overlap = quarter sphere, centroid (3r/8, 3r/8, 0); A's two faces inside B = two quarter disks; B's surface inside A = quarter sphere";
  p.a = op_box("A", f.P(0, 0, -5), f.P(10, 10, 5));
  p.b = op_sphere("B", f.P(0, 0, 0), f.L(2), gen_axis2());
  p.Va = f.V(1000); p.Vb = f.V(sph_vol(2)); p.Vi = f.V(sph_vol(2) / 4);
  p.cA = f.P(5, 5, 0); p.cB = f.P(0, 0, 0); p.cI = f.P(0.75, 0.75, 0);
  p.Sa = f.A(600); p.Sb = f.A(sph_area(2)); p.SaIn = f.A(PI * 4 / 2); p.SbIn = f.A(sph_area(2) / 4);
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(100 + 100 + 100)); p.far = f.far();
  return p;
}

inline Pair sph_in_box(const Frame& f) {
  Pair p;
  p.stem = "SPH-IN-BOX"; p.category = "curved";
  p.descr = "sphere r=2 strictly inside the cube [-5,5]^3";
  p.oracle = "containment: union is the cube, intersection the sphere, A-B the cube with a spherical cavity, B-A empty";
  p.a = op_box("A", f.P(-5, -5, -5), f.P(5, 5, 5));
  p.b = op_sphere("B", f.P(0, 0, 0), f.L(2), gen_axis1());
  p.Va = f.V(1000); p.Vb = f.V(sph_vol(2)); p.Vi = p.Vb;
  p.cA = p.cB = p.cI = f.P(0, 0, 0);
  p.Sa = f.A(600); p.Sb = f.A(sph_area(2)); p.SaIn = 0; p.SbIn = p.Sb;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(4, 2); p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(300.0)); p.far = f.far();
  return p;
}

inline Pair sph_sph(const Frame& f) {
  Pair p;
  const double R = 2, r = 1.5, d = 2;
  const double x0 = (d * d + R * R - r * r) / (2 * d), hA = R - x0, hB = r - (d - x0);
  const double vA = cf::cap_vol(R, hA), vB = cf::cap_vol(r, hB);
  const double xA = cf::cap_centroid(R, hA), xB = d - cf::cap_centroid(r, hB);
  p.stem = "SPH-SPH"; p.category = "curved";
  p.descr = "sphere R=2 at the origin and sphere r=1.5 centred at x=2 (generic pole axes)";
  p.oracle = "lens = two spherical caps cut by the radical plane x0 = (d^2+R^2-r^2)/2d; centroid from cap moments; surfaces inside = cap areas 2 pi R h";
  p.a = op_sphere("A", f.P(0, 0, 0), f.L(R), gen_axis1());
  p.b = op_sphere("B", f.P(2, 0, 0), f.L(r), gen_axis2());
  p.Va = f.V(sph_vol(R)); p.Vb = f.V(sph_vol(r)); p.Vi = f.V(vA + vB);
  p.cA = f.P(0, 0, 0); p.cB = f.P(2, 0, 0); p.cI = f.P((vA * xA + vB * xB) / (vA + vB), 0, 0);
  p.Sa = f.A(sph_area(R)); p.Sb = f.A(sph_area(r)); p.SaIn = f.A(cf::cap_area(R, hA)); p.SbIn = f.A(cf::cap_area(r, hB));
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = f.L(5.5); p.far = f.far();
  return p;
}

inline Pair sph_cyl_x(const Frame& f) {
  Pair p;
  const double R = 2, r = 1, hh = std::sqrt(R * R - r * r), cap = R - hh;
  p.stem = "SPH-CYL-X"; p.category = "curved";
  p.descr = "sphere R=2 (generic pole axis) with an x-axis cylinder r=1, length 8, through its centre";
  p.oracle = "sphere core within r of a diameter = 4/3 pi (R^3-(R^2-r^2)^1.5); sphere surface inside = two caps 2 pi R (R-sqrt(R^2-r^2)); wall inside = 2 pi r x 2 sqrt(R^2-r^2)";
  p.a = op_sphere("A", f.P(0, 0, 0), f.L(R), gen_axis1());
  p.b = op_cyl("B", f.P(-4, 0, 0), {1, 0, 0}, f.L(r), f.L(8));
  p.Va = f.V(sph_vol(R)); p.Vb = f.V(cyl_vol(r, 8)); p.Vi = f.V(4.0 / 3 * PI * (R * R * R - std::pow(R * R - r * r, 1.5)));
  p.cA = p.cB = p.cI = f.P(0, 0, 0);
  p.Sa = f.A(sph_area(R)); p.Sb = f.A(cyl_area(r, 8)); p.SaIn = f.A(2 * cf::cap_area(R, cap)); p.SbIn = f.A(2 * PI * r * 2 * hh);
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = f.L(8.5); p.far = f.far();
  return p;
}

inline Pair box_cone_slab(const Frame& f) {
  Pair p;
  const double r0 = 1.5, h = 3;  // cone apex (0,0,3), base z=0 radius 1.5; slab z in [1,2]
  auto rad = [](double z) { return 0.5 * (3 - z); };
  const double Vi = cf::integrate([&](double z) { return PI * rad(z) * rad(z); }, 1, 2);
  const double Mz = cf::integrate([&](double z) { return z * PI * rad(z) * rad(z); }, 1, 2);
  const double slant = std::sqrt(1.0 + 0.25);
  p.stem = "BOX-CONE-SLAB"; p.category = "curved";
  p.descr = "slab [-3,3]^2 x [1,2] cut by a cone (apex (0,0,3) pointing up, base z=0 radius 1.5)";
  p.oracle = "overlap = pi int_1^2 r(z)^2 dz, r = (3-z)/2; centroid z from the first moment; frustum wall = pi (r1+r2) s";
  p.a = op_box("A", f.P(-3, -3, 1), f.P(3, 3, 2));
  p.b = op_cone("B", f.P(0, 0, 3), {0, 0, -1}, f.L(h), f.L(r0));
  p.Va = f.V(36); p.Vb = f.V(cf::cone_vol(r0, h)); p.Vi = f.V(Vi);
  p.cA = f.P(0, 0, 1.5); p.cB = f.P(0, 0, 0.75); p.cI = f.P(0, 0, Mz / Vi);
  p.Sa = f.A(96); p.Sb = f.A(cf::cone_area(r0, h)); p.SaIn = f.A(1.25 * PI); p.SbIn = f.A(PI * 1.5 * slant);
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = f.L(std::sqrt(36 + 36 + 9)); p.far = f.far();
  return p;
}

inline Pair torus_box_half(const Frame& f) {
  Pair p;
  const double R = 3, r = 1;
  const double Vt = cf::torus_vol(R, r);
  p.stem = "TORUS-BOX-HALF"; p.category = "curved";
  p.descr = "torus (axis z, R=3, r=1) cut by the box [0,10]x[-10,10]^2 whose face x=0 contains the torus axis";
  p.oracle = "overlap = half torus pi^2 R r^2, centroid x = (2R/pi)(1 + r^2/(4R^2)); A's face inside B = two tube disks";
  p.a = op_box("A", f.P(0, -10, -10), f.P(10, 10, 10));
  p.b = op_torus("B", f.P(0, 0, 0), {0, 0, 1}, f.L(R), f.L(r));
  p.Va = f.V(8000); p.Vb = f.V(Vt); p.Vi = f.V(Vt / 2);
  p.cA = f.P(5, 0, 0); p.cB = f.P(0, 0, 0); p.cI = f.P(2 * R / PI * (1 + r * r / (4 * R * R)), 0, 0);
  p.Sa = f.A(1600); p.Sb = f.A(cf::torus_area(R, r)); p.SaIn = f.A(2 * PI * r * r); p.SbIn = f.A(cf::torus_area(R, r) / 2);
  p.tU = T(0, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(100 + 400 + 400)); p.far = f.far();
  return p;
}

// T junction: a y-axis cylinder r=0.5 starting on the axis of a vertical cylinder r=1, its end cap inside.
inline Pair cyl_tpipe(const Frame& f) {
  Pair p;
  const double ra = 1, rb = 0.5;
  const double full = cf::perp_cyl_vol(ra, rb, 0);
  const double my = cf::integrate([&](double x) { return (ra * ra - x * x) * std::sqrt(std::fmax(0, rb * rb - x * x)); }, -rb, rb);
  p.stem = "CYL-TPIPE"; p.category = "curved";
  p.descr = "vertical cylinder r=1 (z in [0,4]) with a horizontal y-axis cylinder r=0.5 (y in [0,3]) starting on its axis at z=2";
  p.oracle = "overlap = half of the full crossing (y >= 0); centroid y = int (ra^2-x^2) sqrt(rb^2-x^2) dx / V; B's end cap (pi rb^2) lies inside A";
  p.a = op_cyl("A", f.P(0, 0, 0), {0, 0, 1}, f.L(ra), f.L(4));
  p.b = op_cyl("B", f.P(0, 0, 2), {0, 1, 0}, f.L(rb), f.L(3));
  p.Va = f.V(cyl_vol(ra, 4)); p.Vb = f.V(cyl_vol(rb, 3)); p.Vi = f.V(full / 2);
  p.cA = f.P(0, 0, 2); p.cB = f.P(0, 1.5, 2); p.cI = f.P(0, my / (full / 2), 2);
  p.Sa = f.A(cyl_area(ra, 4)); p.Sb = f.A(cyl_area(rb, 3));
  p.SaIn = f.A(cf::perp_cyl_wallA_in_B(ra, rb, 0) / 2);
  p.SbIn = f.A(PI * rb * rb + cf::perp_cyl_wallB_in_A(ra, rb, 0) / 2);
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = f.L(std::sqrt(4 + 9 + 16)); p.far = f.far();
  return p;
}

}  // namespace golden
}  // namespace corpus
