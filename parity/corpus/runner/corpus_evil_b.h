// Evil configurations, part B: slivers, near-degenerate edges, many-hole plates, nested
// cutters, periodic seams, apex and pole cases, singular crossings and extreme size ratios.
// (Scale and far-origin ladders reuse the golden configurations through a Frame.)
#pragma once
#include "corpus_evil_a.h"

namespace corpus {
namespace evil {

// ---- sliver: a 10 x 10 x 1e-4 plate pierced by a cylinder r=1
inline Pair thin_plate() {
  Pair p;
  const double t = 1e-4;
  p.stem = "SLIVER-PLATE"; p.category = "sliver";
  p.descr = "plate 10 x 10 x 1e-4 (aspect 1e5) pierced by a z-axis cylinder r=1 spanning z in [-1,1]";
  p.oracle = "overlap = pi r^2 t; the plate's two faces lose a disk each, the wall adds 2 pi r t";
  p.a = op_box("A", {0, 0, 0}, {10, 10, t});
  p.b = op_cyl("B", {5, 5, -1}, {0, 0, 1}, 1, 2);
  p.Va = 100 * t; p.Vb = 2 * PI; p.Vi = PI * t;
  p.cA = {5, 5, t / 2}; p.cB = {5, 5, 0}; p.cI = {5, 5, t / 2};
  p.Sa = 200 + 40 * t; p.Sb = 6 * PI; p.SaIn = 2 * PI; p.SbIn = 2 * PI * t;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = std::sqrt(200 + 4);
  return p;
}
// ---- sliver: a needle cylinder r=1e-3 through a 10-cube
inline Pair needle() {
  Pair p;
  const double r = 1e-3;
  p.stem = "SLIVER-NEEDLE"; p.category = "sliver";
  p.descr = "needle cylinder r=1e-3, length 12, through the cube [0,10]^3 (radius / size = 1e-4)";
  p.oracle = "overlap = pi r^2 x 10; cube faces lose a disk each, hole wall 2 pi r x 10";
  p.a = op_box("A", {0, 0, 0}, {10, 10, 10});
  p.b = op_cyl("B", {5, 5, -1}, {0, 0, 1}, r, 12);
  p.Va = 1000; p.Vb = PI * r * r * 12; p.Vi = PI * r * r * 10;
  p.cA = p.cB = p.cI = {5, 5, 5};
  p.Sa = 600; p.Sb = 2 * PI * r * r + 2 * PI * r * 12; p.SaIn = 2 * PI * r * r; p.SbIn = 2 * PI * r * 10;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = std::sqrt(300 + 144);
  return p;
}
// ---- sliver: a thin wedge (apex angle 1e-4) cut by a box
inline Pair wedge() {
  Pair p;
  const double h = 1e-3;  // wedge: triangle (0,0),(10,0),(10,h), z in [0,1]
  auto hy = [&](double x) { return h * x / 10; };
  const double Vi = cf::integrate([&](double x) { return hy(x) * 0.5; }, 4, 6);
  const double Mx = cf::integrate([&](double x) { return x * hy(x) * 0.5; }, 4, 6);
  const double My = cf::integrate([&](double x) { return hy(x) * hy(x) / 2 * 0.5; }, 4, 6);
  p.stem = "SLIVER-WEDGE"; p.category = "sliver";
  p.descr = "thin wedge (triangle (0,0),(10,0),(10,1e-3) x z in [0,1]) cut across by the box [4,6]x[-1,1]x[0.25,0.75]";
  p.oracle = "overlap = int_4^6 (h x/10) dx x 0.5 (z extent); the box's slot separates it into two pieces (B-A)";
  p.a = op_prism("A", {{0, 0}, {10, 0}, {10, h}}, 0, 1);
  p.b = op_box("B", {4, -1, 0.25}, {6, 1, 0.75});
  p.Va = 0.5 * 10 * h; p.Vb = 2 * 2 * 0.5; p.Vi = Vi;
  p.cA = {20.0 / 3, h / 3, 0.5}; p.cB = {5, 0, 0.5}; p.cI = {Mx / Vi, My / Vi, 0.5};
  p.has_s = false;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.planar = true;
  p.L = std::sqrt(100 + 4 + 1);
  return p;
}
// ---- sliver: a thin square bar (half width w) across a unit sphere
inline Pair bar_sphere() {
  Pair p;
  const double w = 1e-3;
  p.stem = "SLIVER-BAR"; p.category = "sliver";
  p.descr = "thin square bar (2e-3 x 2e-3 x 4) along x through the centre of a unit sphere";
  p.oracle = "overlap = (2w)^2 x 2 x mean sqrt(1 - y^2 - z^2) = 8 w^2 (1 - w^2/3 + ...) over the square";
  p.a = op_sphere("A", {0, 0, 0}, 1, gen_axis1());
  p.b = op_box("B", {-2, -w, -w}, {2, w, w});
  const double mean = cf::integrate([&](double y) { return cf::integrate([&](double z) { return std::sqrt(1 - y * y - z * z); }, -w, w, 60); }, -w, w, 60) / (4 * w * w);
  p.Va = sph_vol(1); p.Vb = 16 * w * w; p.Vi = 8 * w * w * mean;
  p.cA = p.cB = p.cI = {0, 0, 0}; p.has_s = false;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = 4.5;
  return p;
}

// ---- boxes touching along an edge / overlapping a corner column by delta (B is raised so no faces are coplanar)
inline Pair edge_contact(double delta) {
  Pair p;
  p.stem = "EDGE-CONTACT"; p.category = "degenerate"; p.planar = true;
  p.descr = std::string("cube [0,2]^3 and the cube [2-d,4-d]^2 x [0.5,2.5] ") + (delta == 0 ? "touching only along the vertical edge x=y=2" : "overlapping in a column of cross-section d x d, d = " + tag(delta));
  p.oracle = "overlap = d x d x 1.5; for d = 0 the union is two shells sharing an edge (non-manifold)";
  p.a = op_box("A", {0, 0, 0}, {2, 2, 2});
  p.b = op_box("B", {2 - delta, 2 - delta, 0.5}, {4 - delta, 4 - delta, 2.5});
  p.Va = p.Vb = 8; p.Vi = 1.5 * delta * delta;
  p.cA = {1, 1, 1}; p.cB = {3 - delta, 3 - delta, 1.5}; p.cI = {2 - delta / 2, 2 - delta / 2, 1.25};
  p.Sa = p.Sb = 24; p.SaIn = p.SbIn = 3 * delta + delta * delta;
  p.tU = delta > 0 ? T(2, 1) : TNone(); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = std::sqrt(16 + 16 + 6);
  return p;
}
inline Pair vertex_contact() {
  Pair p;
  p.stem = "VERTEX-CONTACT"; p.category = "degenerate"; p.planar = true;
  p.descr = "cubes [0,2]^3 and [2,4]^3 touching at the single point (2,2,2)";
  p.oracle = "two shells meeting at a point: union volume 16 (non-manifold at the vertex), intersection empty";
  p.a = op_box("A", {0, 0, 0}, {2, 2, 2});
  p.b = op_box("B", {2, 2, 2}, {4, 4, 4});
  p.Va = p.Vb = 8; p.Vi = 0;
  p.cA = {1, 1, 1}; p.cB = {3, 3, 3};
  p.Sa = p.Sb = 24;
  p.tU = TNone(); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = std::sqrt(48.0);
  return p;
}
// ---- a unit cube turned 45 degrees about its lowest edge, resting on (or sunk delta into) the top face of [0,4]^2 x [0,2]
inline Pair edge_on_face(double delta) {
  Pair p;
  p.stem = "EDGE-ON-FACE"; p.category = "degenerate"; p.planar = true;
  const double zb = 2 - delta;
  p.descr = std::string("unit cube turned 45 degrees about its lowest edge, which lies ") + (delta == 0 ? "exactly on" : tag(delta) + " below") + " the top face z=2 of the box [0,4]^2 x [0,2]";
  p.oracle = "line contact for d = 0 (union volume 33, non-manifold); sunk edge makes a right-triangle prism of area d^2 and length 1: overlap = d^2";
  p.a = op_box("A", {0, 0, 0}, {4, 4, 2});
  p.b = op_box_rot("B", {1.5, 1.5, zb}, {2.5, 2.5, zb + 1}, {1, 0, 0}, 45.0, {2, 1.5, zb});
  p.Va = 32; p.Vb = 1; p.Vi = delta * delta;
  p.cA = {2, 2, 2 - 1}; p.has_c = false; p.has_s = false;
  p.tU = delta > 0 ? T(2, 1) : TNone(); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = std::sqrt(16 + 16 + 4);
  return p;
}

// ---- seams, poles and apexes ------------------------------------------------------------
// cylinder (seam at +x, through the axis) cut by a half-space box whose face y=0 contains the seam line
inline Pair seam_cyl_half() {
  Pair p;
  p.stem = "SEAM-CYL-HALF"; p.category = "seam";
  p.descr = "z-axis cylinder r=1, z in [0,4] (seam at +x) against the box [-2,2]x[0,2]x[-1,5] whose face y=0 contains the seam line and the axis";
  p.oracle = "overlap = half cylinder 2 pi, centroid y = 4 r/(3 pi); A's surface inside B = half of 10 pi; B's face inside A = 2 x 4";
  p.a = op_cyl("A", {0, 0, 0}, {0, 0, 1}, 1, 4);
  p.b = op_box("B", {-2, 0, -1}, {2, 2, 5});
  p.Va = 4 * PI; p.Vb = 48; p.Vi = 2 * PI;
  p.cA = {0, 0, 2}; p.cB = {0, 1, 2}; p.cI = {0, 4 / (3 * PI), 2};
  p.Sa = 10 * PI; p.Sb = 88; p.SaIn = 5 * PI; p.SbIn = 8;
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = std::sqrt(16 + 4 + 36);
  return p;
}
// sphere (poles on z, seam meridian at +x) cut by the half-space box [-5,5]x[0,5]x[-5,5]: the face y=0 holds the poles, the seam and the axis
inline Pair seam_sph_half() {
  Pair p;
  p.stem = "SEAM-SPH-HALF"; p.category = "seam";
  p.descr = "sphere r=2 (poles on z, seam meridian at +x) cut by the box [-5,5]x[0,5]x[-5,5]: its face y=0 contains both poles and the seam";
  p.oracle = "overlap = hemisphere 16 pi/3, centroid 3 r/8 along y; A's surface inside B = half of 16 pi; B's face inside A = disk 4 pi";
  p.a = op_sphere("A", {0, 0, 0}, 2, {0, 0, 1});
  p.b = op_box("B", {-5, 0, -5}, {5, 5, 5});
  p.Va = sph_vol(2); p.Vb = 500; p.Vi = sph_vol(2) / 2;
  p.cA = {0, 0, 0}; p.cB = {0, 2.5, 0}; p.cI = {0, 0.75, 0};
  p.Sa = sph_area(2); p.Sb = 400; p.SaIn = sph_area(2) / 2; p.SbIn = 4 * PI;
  p.tU = p.tI = p.tAB = p.tBA = T(2, 1);
  p.L = std::sqrt(100 + 25 + 100);
  return p;
}
// torus (axis z, seam circles) cut by the half-space box [-10,10]x[0,10]x[-10,10]: its face y=0 contains the u-seam circle at +x
inline Pair seam_torus_half() {
  Pair p;
  const double R = 3, r = 1, Vt = cf::torus_vol(R, r);
  p.stem = "SEAM-TORUS-HALF"; p.category = "seam";
  p.descr = "torus (axis z, R=3, r=1) cut by the box [-10,10]x[0,10]x[-10,10]: its face y=0 contains the torus axis and the seam meridian circle at +x";
  p.oracle = "overlap = half torus pi^2 R r^2, centroid y = (2R/pi)(1 + r^2/(4R^2)); A's surface inside B = half; B's face inside A = two tube disks";
  p.a = op_torus("A", {0, 0, 0}, {0, 0, 1}, R, r);
  p.b = op_box("B", {-10, 0, -10}, {10, 10, 10});
  p.Va = Vt; p.Vb = 4000; p.Vi = Vt / 2;
  p.cA = {0, 0, 0}; p.cB = {0, 5, 0}; p.cI = {0, 2 * R / PI * (1 + r * r / (4 * R * R)), 0};
  p.Sa = cf::torus_area(R, r); p.Sb = 1600; p.SaIn = p.Sa / 2; p.SbIn = 2 * PI * r * r;
  p.tU = T(0, 1); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(0, 1);
  p.L = std::sqrt(400 + 100 + 400);
  return p;
}
// a transverse hole centred exactly on the seam line of a cylinder
inline Pair seam_hole() {
  Pair p;
  const double ra = 2, rb = 0.5;
  p.stem = "SEAM-HOLE"; p.category = "seam";
  p.descr = "z-axis cylinder r=2 (z in [0,4], seam at +x) crossed by an x-axis cylinder r=0.5 whose axis passes through the seam line at (2,0,2)";
  p.oracle = "overlap = int 4 sqrt(ra^2-y^2) sqrt(rb^2-y^2) dy (Gauss-Legendre); walls inside by quadrature";
  p.a = op_cyl("A", {0, 0, 0}, {0, 0, 1}, ra, 4);
  p.b = op_cyl("B", {-3, 0, 2}, {1, 0, 0}, rb, 6);
  p.Va = cyl_vol(ra, 4); p.Vb = cyl_vol(rb, 6); p.Vi = cf::perp_cyl_vol(ra, rb, 0);
  p.cA = p.cB = p.cI = {0, 0, 2};
  p.Sa = cyl_area(ra, 4); p.Sb = cyl_area(rb, 6);
  p.SaIn = cf::perp_cyl_wallA_in_B(ra, rb, 0); p.SbIn = cf::perp_cyl_wallB_in_A(ra, rb, 0);
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = std::sqrt(16 + 16 + 36);
  return p;
}
// cylinder r=1 through both poles of a sphere R=2
inline Pair pole_cyl() {
  Pair p;
  const double R = 2, r = 1, hh = std::sqrt(R * R - r * r);
  p.stem = "POLE-CYL"; p.category = "pole";
  p.descr = "sphere R=2 with poles on z and a z-axis cylinder r=1, z in [-3,3], through both poles";
  p.oracle = "sphere core within r of the axis: 4/3 pi (R^3-(R^2-r^2)^1.5); polar caps of height R - sqrt(R^2-r^2); wall inside 2 pi r x 2 sqrt(R^2-r^2)";
  p.a = op_sphere("A", {0, 0, 0}, R, {0, 0, 1});
  p.b = op_cyl("B", {0, 0, -3}, {0, 0, 1}, r, 6);
  p.Va = sph_vol(R); p.Vb = cyl_vol(r, 6); p.Vi = 4.0 / 3 * PI * (R * R * R - std::pow(R * R - r * r, 1.5));
  p.cA = p.cB = p.cI = {0, 0, 0};
  p.Sa = sph_area(R); p.Sb = cyl_area(r, 6); p.SaIn = 2 * cf::cap_area(R, R - hh); p.SbIn = 2 * PI * r * 2 * hh;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = 6.5;
  return p;
}
// cone (apex up) standing on its tip at the top face z=2 of [-2,2]^3, sunk by delta
inline Pair apex_box(double delta) {
  Pair p;
  const double ztip = 2 - delta;
  p.stem = "APEX-BOX"; p.category = "apex";
  p.descr = std::string("cone (apex down, height 2, base radius 1) whose tip is ") + (delta == 0 ? "exactly on" : tag(delta) + " below") + " the top face z=2 of the cube [-2,2]^3";
  p.oracle = "tip inside the cube is a small cone of height delta and radius delta/2: pi delta^3/12; centroid 3/4 of its height from the tip";
  p.a = op_box("A", {-2, -2, -2}, {2, 2, 2});
  p.b = op_cone("B", {0, 0, ztip}, {0, 0, 1}, 2, 1);
  p.Va = 64; p.Vb = 2 * PI / 3; p.Vi = PI * delta * delta * delta / 12;
  p.cA = {0, 0, 0}; p.cB = {0, 0, ztip + 1.5}; p.cI = {0, 0, ztip + 0.75 * delta};
  p.has_s = false; p.has_c = delta == 0 ? false : true;
  p.tU = delta > 0 ? T(2, 1) : TNone(); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = std::sqrt(16 + 16 + 25);
  return p;
}
// cone with its apex on the axis of a coaxial cylinder
inline Pair apex_cyl() {
  Pair p;
  const double h = 3, r = 1.5, rc = 0.5;  // cone: apex (0,0,1) opening up to the base at z=4
  const double Vi = PI / 12 + PI * rc * rc * 2;
  const double zi = ((PI / 12) * (1 + 0.75) + (PI * rc * rc * 2) * 3) / Vi;
  const double slant = std::sqrt(rc * rc + 1.0);
  p.stem = "APEX-CYL"; p.category = "apex";
  p.descr = "cone (apex (0,0,1), height 3, base radius 1.5) and a coaxial cylinder r=0.5, z in [0,5], whose axis carries the apex";
  p.oracle = "overlap = cone of height 1 (pi/12) plus cylinder r=0.5 over cone heights [1,3] (pi/2)";
  p.a = op_cone("A", {0, 0, 1}, {0, 0, 1}, h, r);
  p.b = op_cyl("B", {0, 0, 0}, {0, 0, 1}, rc, 5);
  p.Va = cf::cone_vol(r, h); p.Vb = cyl_vol(rc, 5); p.Vi = Vi;
  p.cA = {0, 0, 4 - 0.75}; p.cB = {0, 0, 2.5}; p.cI = {0, 0, zi};
  p.Sa = cf::cone_area(r, h); p.Sb = cyl_area(rc, 5);
  p.SaIn = PI * rc * slant; p.SbIn = 2 * PI * rc * 2;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = std::sqrt(9 + 9 + 25);
  return p;
}
// cone whose apex sits on the pole of a sphere, pointing inward
inline Pair apex_pole() {
  Pair p;
  p.stem = "APEX-POLE"; p.category = "apex";
  p.descr = "sphere R=2 (pole (0,0,2)) and an inward cone with apex exactly on that pole (height 2, base radius 0.5)";
  p.oracle = "the cone lies inside the sphere and touches its surface only at the shared apex/pole: U = sphere, I = cone, A-B = sphere with a cavity touching its surface at one point";
  p.a = op_sphere("A", {0, 0, 0}, 2, {0, 0, 1});
  p.b = op_cone("B", {0, 0, 2}, {0, 0, -1}, 2, 0.5);
  p.Va = sph_vol(2); p.Vb = cf::cone_vol(0.5, 2); p.Vi = p.Vb;
  p.cA = {0, 0, 0}; p.cB = p.cI = {0, 0, 0.5};
  p.Sa = sph_area(2); p.Sb = cf::cone_area(0.5, 2); p.SaIn = 0; p.SbIn = p.Sb;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = TNone(); p.tBA = T(2, 1);
  p.L = 4.3;
  return p;
}
// two equal cylinders crossing at right angles (singular intersection curves meet at two points)
inline Pair steinmetz() {
  Pair p;
  p.stem = "STEINMETZ"; p.category = "singular";
  p.descr = "z-axis and x-axis cylinders r=1, length 6, axes crossing: the two intersection ellipses meet at (0,+-1,0)";
  p.oracle = "Steinmetz solid 16 r^3/3; wall areas inside = 8 each (bicylinder surface 16)";
  p.a = op_cyl("A", {0, 0, -3}, {0, 0, 1}, 1, 6);
  p.b = op_cyl("B", {-3, 0, 0}, {1, 0, 0}, 1, 6);
  p.Va = p.Vb = 6 * PI; p.Vi = 16.0 / 3;
  p.cA = p.cB = p.cI = {0, 0, 0};
  p.Sa = p.Sb = 14 * PI; p.SaIn = p.SbIn = 8;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = TNone(); p.tBA = TNone();
  p.L = std::sqrt(36 + 4 + 4);
  return p;
}
// extreme size ratio: a 1e-3 hole through a 1e3 cube
inline Pair ratio_hole() {
  Pair p;
  const double r = 1e-3;
  p.stem = "RATIO-HOLE"; p.category = "scale-ratio";
  p.descr = "cube [0,1000]^3 with a through hole of radius 1e-3 (size ratio 1e6)";
  p.oracle = "volume 1e9 - pi r^2 x 1000 (below the numeric floor of the volume check; the topology, genus 1, is what is asserted)";
  p.a = op_box("A", {0, 0, 0}, {1000, 1000, 1000});
  p.b = op_cyl("B", {500, 500, -1}, {0, 0, 1}, r, 1002);
  p.Va = 1e9; p.Vb = PI * r * r * 1002; p.Vi = PI * r * r * 1000;
  p.cA = p.cB = p.cI = {500, 500, 500}; p.has_s = false;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(0, 1); p.tBA = T(4, 2);
  p.L = std::sqrt(3e6);
  return p;
}
inline Pair ratio_boss() {
  Pair p;
  p.stem = "RATIO-BOSS"; p.category = "scale-ratio"; p.planar = true;
  p.descr = "cube [0,1000]^3 with a 1e-3 cube standing on its top face (size ratio 1e6, coplanar contact)";
  p.oracle = "union = cube + tiny boss; asserted through topology (one shell) because the volume of the boss is below the numeric floor";
  p.a = op_box("A", {0, 0, 0}, {1000, 1000, 1000});
  p.b = op_box("B", {500, 500, 1000}, {500.001, 500.001, 1000.001});
  p.Va = 1e9; p.Vb = 1e-9; p.Vi = 0;
  p.cA = {500, 500, 500}; p.cB = {500.0005, 500.0005, 1000.0005}; p.has_s = false; p.has_c = false;
  p.tU = T(2, 1); p.tI = T(2, 1); p.tAB = T(2, 1); p.tBA = T(2, 1);
  p.L = std::sqrt(3e6);
  return p;
}

}  // namespace evil
}  // namespace corpus
