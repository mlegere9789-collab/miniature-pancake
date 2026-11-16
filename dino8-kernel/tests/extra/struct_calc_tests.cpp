// Structural take-off and section-property formulas (include/dino8/kernel/struct_calc.h). One test per Register row; every expected
// value is either a hand-derived closed form (derivation in the comment of each test, literals computed with exact rational
// arithmetic outside the code under test) or the result of a route that shares no code with the function: brute-force numeric
// integration of the section, trial loops for bar counts, exhaustive integer-millimetre arithmetic for rounding.
// The "vendor sentence" cited per test is the statement of the Register row; its source is the page named in the row.
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <string>
#include <vector>

#include "dino8/kernel/struct_calc.h"
#include "test_registry.h"

namespace {

using namespace dino8::kernel;

bool Near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }
bool NearRel(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::max(std::fabs(a), std::fabs(b)); }

// Midpoint-rule integration of a shape given as an indicator over its bounding box [0,w] x [0,h]: area, centroid height and the
// second moments about the centroidal axes. No formula of struct_calc is used.
struct Numeric {
  double area = 0, ybar = 0, ix = 0, iy = 0;
};
Numeric Integrate(double w, double h, int nx, int ny, const std::function<bool(double, double)>& inside) {
  const double dx = w / nx, dy = h / ny, dA = dx * dy;
  double a = 0, sy = 0, sx = 0;
  for (int i = 0; i < nx; ++i)
    for (int j = 0; j < ny; ++j) {
      const double x = (i + 0.5) * dx, y = (j + 0.5) * dy;
      if (!inside(x, y)) continue;
      a += dA; sy += y * dA; sx += x * dA;
    }
  Numeric n;
  n.area = a;
  n.ybar = sy / a;
  const double xbar = sx / a;
  for (int i = 0; i < nx; ++i)
    for (int j = 0; j < ny; ++j) {
      const double x = (i + 0.5) * dx, y = (j + 0.5) * dy;
      if (!inside(x, y)) continue;
      n.ix += (y - n.ybar) * (y - n.ybar) * dA;
      n.iy += (x - xbar) * (x - xbar) * dA;
    }
  return n;
}

}  // namespace

// D8-REVIT-STRUCT-0104: "Calculate cross-section properties of common concrete framing shapes from their geometry into analytical
// parameters, updating on dimension changes" (Concrete Framing: Automatic Cross Section Properties).
// Rectangle 0.3 x 0.6: A = 0.18, Ix = b h^3 / 12 = 0.3 * 0.216 / 12 = 0.0054, Iy = h b^3 / 12 = 0.6 * 0.027 / 12 = 0.00135,
//   Zx = Ix / (h / 2) = 0.018, Zy = Iy / 0.15 = 0.009, plastic Zx = b h^2 / 4 = 0.027, rx = h / sqrt(12) = 0.173205.
// Circle d = 0.4: A = pi d^2 / 4 = 0.1256637, I = pi d^4 / 64 = 1.2566371e-3, Z = pi d^3 / 32 = 6.2831853e-3, J = pi d^4 / 32.
// T section flange 0.6 x 0.15, stem 0.3, depth 0.5 (exact rational arithmetic): A = 0.195, ybar = 0.29038462 from the bottom,
//   Ix = 0.0042694712, Iy = 0.0034875, Zx_top = Ix / 0.20961538 = 0.020368119, Zx_bottom = Ix / ybar = 0.014702815.
// I section 0.2 x 0.015 flanges, 0.01 web, depth 0.4 (parallel-axis theorem in rational arithmetic): A = 0.0097, Ix = 2.6466083e-4,
//   Iy = 2.0030833e-5, Zx = 1.3233042e-3, plastic Zx = 1.49725e-3.
// Box 0.2 x 0.3, t = 0.02: A = 0.0184, Ix = 2.1565333e-4, Iy = 1.1125333e-4. Tube d = 0.3, t = 0.01: A = 9.1106187e-3,
//   I = 9.5889262e-5, J = 2 I = 1.9177852e-4.
// Each is also checked against brute-force midpoint integration of the section outline, and "updating on dimension changes" by
// the scaling laws (A ~ k^2, I ~ k^4, Z ~ k^3 when all dimensions scale by k).
D8_EXTRA_TEST(StructSectionPropsClosedForm) {
  SectionProps r = RectSection(0.3, 0.6);
  d8test::Check(r.valid && Near(r.area, 0.18, 1e-15) && Near(r.ix, 0.0054, 1e-15) && Near(r.iy, 0.00135, 1e-15), "rectangle 0.3 x 0.6: A 0.18, Ix 0.0054, Iy 0.00135");
  d8test::Check(Near(r.zx, 0.018, 1e-15) && Near(r.zy, 0.009, 1e-15) && Near(r.plastic_zx, 0.027, 1e-15) && Near(r.ybar, 0.3, 1e-15), "rectangle: Zx 0.018, Zy 0.009, plastic Zx 0.027, centroid 0.3");
  d8test::Check(Near(r.rx, 0.6 / std::sqrt(12.0), 1e-12) && Near(r.ry, 0.3 / std::sqrt(12.0), 1e-12), "rectangle: radii of gyration h / sqrt(12), b / sqrt(12)");

  const double pi = 3.14159265358979323846;
  SectionProps c = CircleSection(0.4);
  d8test::Check(Near(c.area, pi * 0.04, 1e-12) && Near(c.ix, 1.2566370614359172e-3, 1e-15) && Near(c.iy, c.ix, 1e-18), "circle d 0.4: A pi * 0.04, I 1.2566371e-3");
  d8test::Check(Near(c.zx, 6.283185307179586e-3, 1e-14) && Near(c.j, 2.5132741228718345e-3, 1e-14) && Near(c.plastic_zx, 0.064 / 6.0, 1e-15), "circle: Z 6.2831853e-3, J 2.5132741e-3, plastic d^3 / 6");

  SectionProps t = TSection(0.6, 0.15, 0.3, 0.5);
  d8test::Check(Near(t.area, 0.195, 1e-15) && Near(t.ybar, 0.2903846153846154, 1e-13), "T section: A 0.195, centroid 0.29038462 above the stem bottom");
  d8test::Check(Near(t.ix, 0.004269471153846154, 1e-13) && Near(t.iy, 0.0034875, 1e-15), "T section: Ix 0.0042694712, Iy 0.0034875");
  d8test::Check(Near(t.zx_top, 0.020368119266055047, 1e-12) && Near(t.zx_bottom, 0.014702814569536423, 1e-12) && Near(t.zx, t.zx_bottom, 1e-18), "T section: Zx_top 0.020368119, Zx_bottom 0.014702815, governing = the smaller");

  SectionProps i = ISection(0.2, 0.015, 0.01, 0.4);
  d8test::Check(Near(i.area, 0.0097, 1e-15) && Near(i.ix, 2.6466083333333333e-4, 1e-15) && Near(i.iy, 2.0030833333333335e-5, 1e-16), "I section: A 0.0097, Ix 2.6466083e-4, Iy 2.0030833e-5");
  d8test::Check(Near(i.zx, 1.3233041666666668e-3, 1e-14) && Near(i.zy, 2.0030833333333335e-4, 1e-15) && Near(i.plastic_zx, 1.49725e-3, 1e-14), "I section: Zx 1.3233042e-3, Zy 2.0030833e-4, plastic Zx 1.49725e-3");

  SectionProps b = BoxSection(0.2, 0.3, 0.02);
  d8test::Check(Near(b.area, 0.0184, 1e-15) && Near(b.ix, 2.1565333333333333e-4, 1e-15) && Near(b.iy, 1.1125333333333333e-4, 1e-15), "box 0.2 x 0.3 t 0.02: A 0.0184, Ix 2.1565333e-4, Iy 1.1125333e-4");
  SectionProps tube = HollowCircleSection(0.3, 0.01);
  d8test::Check(Near(tube.area, 9.11061869541041e-3, 1e-14) && Near(tube.ix, 9.588926176919458e-5, 1e-16) && Near(tube.j, 1.9177852353838916e-4, 1e-15), "tube d 0.3 t 0.01: A 9.1106187e-3, I 9.5889262e-5, J = 2 I");

  // Brute-force numeric integration of the outlines, independent of every formula above.
  Numeric nr = Integrate(0.3, 0.6, 300, 600, [](double, double) { return true; });
  d8test::Check(NearRel(nr.area, r.area, 1e-9) && NearRel(nr.ix, r.ix, 1e-4) && NearRel(nr.iy, r.iy, 1e-4), "integration of the rectangle agrees");
  Numeric nc = Integrate(0.4, 0.4, 800, 800, [](double x, double y) { return (x - 0.2) * (x - 0.2) + (y - 0.2) * (y - 0.2) <= 0.04; });
  d8test::Check(NearRel(nc.area, c.area, 2e-3) && NearRel(nc.ix, c.ix, 3e-3), "integration of the circle agrees (disc sampling error)");
  Numeric nt = Integrate(0.6, 0.5, 600, 500, [](double x, double y) { return y >= 0.35 || (x >= 0.15 && x <= 0.45); });
  d8test::Check(NearRel(nt.area, t.area, 1e-9) && NearRel(nt.ybar, t.ybar, 1e-6) && NearRel(nt.ix, t.ix, 1e-5) && NearRel(nt.iy, t.iy, 1e-5), "integration of the T section agrees (area, centroid, Ix, Iy)");
  Numeric ni = Integrate(0.2, 0.4, 800, 1600, [](double x, double y) { return y < 0.015 || y > 0.385 || (x >= 0.095 && x <= 0.105); });
  d8test::Check(NearRel(ni.area, i.area, 1e-9) && NearRel(ni.ix, i.ix, 1e-5) && NearRel(ni.iy, i.iy, 1e-5), "integration of the I section agrees");
  Numeric nb = Integrate(0.2, 0.3, 400, 600, [](double x, double y) { return x < 0.02 || x > 0.18 || y < 0.02 || y > 0.28; });
  d8test::Check(NearRel(nb.area, b.area, 1e-9) && NearRel(nb.ix, b.ix, 1e-5) && NearRel(nb.iy, b.iy, 1e-5), "integration of the box agrees");

  // Updating on dimension changes: scaling every dimension by k.
  const double k = 1.7;
  SectionProps big = RectSection(0.3 * k, 0.6 * k);
  d8test::Check(NearRel(big.area, r.area * k * k, 1e-12) && NearRel(big.ix, r.ix * std::pow(k, 4), 1e-12) && NearRel(big.zx, r.zx * k * k * k, 1e-12), "scaling all dimensions by k scales A by k^2, I by k^4, Z by k^3");
  SectionProps wide = RectSection(0.3, 0.7);
  d8test::Check(Near(wide.ix, 0.3 * 0.343 / 12.0, 1e-15), "changing the depth to 0.7 gives Ix = 0.3 * 0.343 / 12");
  d8test::Check(!RectSection(0, 1).valid && !RectSection(1, -1).valid && !CircleSection(0).valid && !ISection(0.2, 0.2, 0.01, 0.4).valid && !BoxSection(0.2, 0.3, 0.15).valid && !TSection(0.1, 0.1, 0.2, 0.4).valid,
                "degenerate dimensions (zero, negative, flanges thicker than the depth, web wider than the flange) are invalid");
}

// D8-REVIT-STRUCT-0083: "A steel member's weight is computed from its section shape and section area parameters together with its
// material, and members lacking those parameters report zero weight" (Calculate the Weight for Structural Steel Elements).
// I section above (A = 0.0097 m^2), length 6 m, steel 7850 kg/m^3: 0.0097 * 6 = 0.0582 m^3, * 7850 = 456.87 kg.
// A 0.3 x 0.3 box tube t 0.01: A = 0.09 - 0.0784 = 0.0116, 4 m: 0.0464 m^3 -> 364.24 kg. Reinforced check: mass = rho * V.
D8_EXTRA_TEST(StructSteelMemberWeight) {
  SectionProps i = ISection(0.2, 0.015, 0.01, 0.4);
  d8test::Check(Near(MemberMass(i.area, 6.0, 7850.0), 456.87, 1e-9), "I section 0.0097 m^2 x 6 m x 7850 = 456.87 kg");
  SectionProps tube = BoxSection(0.3, 0.3, 0.01);
  d8test::Check(Near(tube.area, 0.0116, 1e-15) && Near(MemberMass(tube.area, 4.0, 7850.0), 364.24, 1e-9), "box tube 0.0116 m^2 x 4 m x 7850 = 364.24 kg");
  d8test::Check(MemberMass(0.0, 6.0, 7850.0) == 0.0, "a member without a section area reports zero weight");
  d8test::Check(MemberMass(0.0097, 6.0, 0.0) == 0.0, "a member without a material density reports zero weight");
  d8test::Check(MemberMass(0.0097, 0.0, 7850.0) == 0.0, "a zero length reports zero weight");
  d8test::Check(Near(MemberMass(i.area, 12.0, 7850.0), 2.0 * MemberMass(i.area, 6.0, 7850.0), 1e-9), "weight is linear in length");
  const SectionProps invalid = ISection(0.2, 0.3, 0.01, 0.4);
  d8test::Check(!invalid.valid && MemberMass(invalid.area, 6.0, 7850.0) == 0.0, "an invalid section has no area parameter, so its member weighs zero");
}

// D8-REVIT-STRUCT-0095: "Reinforcement mass is calculated per rebar as total length times a per-type mass per unit length"
// (Easy Rebar Weight Calculation). Mass per metre of a plain round bar = pi d^2 / 4 * 7850:
//   d 12 mm: 1.1309734e-4 m^2 -> 0.887822 kg/m; d 16: 2.0106193e-4 -> 1.578336 kg/m; d 20: 3.1415927e-4 -> 2.466150 kg/m.
// Independent published rule of thumb d^2 / 162 kg/m (d in mm) agrees within 0.5 %. A set of 24 bars of 5.85 m at d 16: 24 * 5.85 *
// 1.578336 = 221.598 kg.
D8_EXTRA_TEST(StructRebarMassFromLength) {
  const double m12 = RebarMassPerLength(0.012), m16 = RebarMassPerLength(0.016), m20 = RebarMassPerLength(0.020);
  d8test::Check(Near(m12, 0.8878, 5e-5) && Near(m16, 1.5783, 5e-5) && Near(m20, 2.4662, 5e-5), "mass per metre of 12, 16, 20 mm bars: 0.8878, 1.5783, 2.4662 kg/m");
  d8test::Check(Near(m16, 3.14159265358979 * 0.016 * 0.016 / 4.0 * 7850.0, 1e-9), "d = 16 mm: pi * 0.016^2 / 4 * 7850");
  for (double dmm : {10.0, 12.0, 16.0, 20.0, 25.0, 32.0}) d8test::Check(NearRel(RebarMassPerLength(dmm / 1000.0), dmm * dmm / 162.0, 5e-3), "rule of thumb d^2 / 162 kg/m agrees within 0.5 %");
  d8test::Check(Near(RebarMass(5.85, m16), 5.85 * 1.5783361, 1e-4), "one 5.85 m bar of d 16: 9.2334 kg");
  d8test::Check(Near(RebarSetMass(24, 5.85, m16), 24.0 * 5.85 * 1.5783361, 1e-3) && Near(RebarSetMass(24, 5.85, m16), 221.598, 1e-3), "a set of 24 bars: 221.598 kg");
  d8test::Check(RebarMass(-1.0, m16) == 0.0 && RebarSetMass(-3, 5.0, m16) == 0.0 && RebarMassPerLength(0.0) == 0.0, "negative lengths, counts and a zero diameter give zero");
  d8test::Check(Near(RebarMassPerLength(0.016, 7850.0 * 2.0), 2.0 * m16, 1e-12), "mass per metre is linear in the density of the rebar type");
}

// D8-REVIT-STRUCT-0076: "Lap length is computed as bar diameter times a lap multiplier defined on the splice type, per rebar type,
// and can be overridden manually" (Rebar Splice Length Parameters).
// d 16 mm * 40 = 0.640 m; d 25 mm * 40 = 1.000 m; a different splice type with multiplier 50 on d 16: 0.800 m; override 0.55 wins.
D8_EXTRA_TEST(StructRebarLapLength) {
  d8test::Check(Near(LapLength(0.016, 40.0), 0.640, 1e-12), "d 16 mm x 40 = 0.640 m");
  d8test::Check(Near(LapLength(0.025, 40.0), 1.000, 1e-12), "d 25 mm x 40 = 1.000 m");
  d8test::Check(Near(LapLength(0.016, 50.0), 0.800, 1e-12), "the multiplier belongs to the splice type: x 50 gives 0.800 m");
  d8test::Check(Near(LapLength(0.016, 40.0, 0.55), 0.55, 1e-15), "a manual override of 0.55 replaces the computed value");
  d8test::Check(Near(LapLength(0.016, 40.0, 0.0), 0.640, 1e-12) && Near(LapLength(0.016, 40.0, -1.0), 0.640, 1e-12), "no override (0 or negative) keeps the computed value");
  d8test::Check(LapLength(0.0, 40.0) == 0.0 && LapLength(0.016, 0.0) == 0.0, "zero diameter or zero multiplier gives zero");
  // A 12 m run spliced once with this lap needs 12 + 0.64 = 12.64 m of bar: two bars of 6.32 m.
  d8test::Check(Near(2.0 * 6.32, 12.0 + LapLength(0.016, 40.0), 1e-12), "two bars covering 12.0 m with one lap of 0.64 m have total length 12.64 m");
}

// D8-REVIT-STRUCT-0077: "Staggered splices offset adjacent spliced bars by a stagger length equal to lap length times a stagger
// multiplier on the splice type" (Rebar Splice Instance and Type Parameters).
// Lap 0.640 * 0.5 = 0.320; lap 1.000 * 1.3 = 1.300; adjacent bars k = 0, 1, 2, 3 sit at offsets k * stagger.
D8_EXTRA_TEST(StructRebarStaggerLength) {
  d8test::Check(Near(StaggerLength(0.640, 0.5), 0.320, 1e-12), "lap 0.640 x stagger multiplier 0.5 = 0.320");
  d8test::Check(Near(StaggerLength(1.000, 1.3), 1.300, 1e-12), "lap 1.000 x 1.3 = 1.300");
  d8test::Check(Near(StaggerLength(LapLength(0.016, 40.0), 0.5), 0.320, 1e-12), "stagger follows the lap computed from the diameter: 0.016 * 40 * 0.5");
  d8test::Check(Near(StaggerLength(LapLength(0.016, 40.0, 0.55), 0.5), 0.275, 1e-12), "an overridden lap of 0.55 feeds the stagger: 0.275");
  const double st = StaggerLength(0.640, 0.5);
  const double expect[4] = {0.0, 0.32, 0.64, 0.96};
  bool ok = true;
  for (int k = 0; k < 4; ++k) ok = ok && Near(k * st, expect[k], 1e-12);
  d8test::Check(ok, "adjacent spliced bars are offset by 0, 0.32, 0.64, 0.96");
  d8test::Check(StaggerLength(0.640, 0.0) == 0.0 && StaggerLength(-1.0, 0.5) == 0.0, "zero multiplier or negative lap gives zero");
}

// D8-REVIT-STRUCT-0067: "Reinforcement settings can round calculated bar lengths ... by a chosen method and increment"
// (Reinforcement Rounding). Hand values for 1234.7 mm: increment 5 mm -> nearest 1235, up 1235, down 1230; increment 10 ->
// 1230, 1240, 1230; increment 25 -> 1225, 1250, 1225. Exhaustive oracle: every integer length 0..3000 mm and increments 5, 10, 25, 50
// 100 compared with integer arithmetic (nearest = half up, up = ceiling, down = floor), computed without floating point.
D8_EXTRA_TEST(StructRebarLengthRounding) {
  const double v = 1.2347;
  d8test::Check(Near(RoundLength(v, RoundMethod::Nearest, 0.005), 1.235, 1e-9) && Near(RoundLength(v, RoundMethod::Up, 0.005), 1.235, 1e-9) && Near(RoundLength(v, RoundMethod::Down, 0.005), 1.230, 1e-9), "1234.7 mm at 5 mm: nearest 1235, up 1235, down 1230");
  d8test::Check(Near(RoundLength(v, RoundMethod::Nearest, 0.010), 1.230, 1e-9) && Near(RoundLength(v, RoundMethod::Up, 0.010), 1.240, 1e-9) && Near(RoundLength(v, RoundMethod::Down, 0.010), 1.230, 1e-9), "1234.7 mm at 10 mm: nearest 1230, up 1240, down 1230");
  d8test::Check(Near(RoundLength(v, RoundMethod::Nearest, 0.025), 1.225, 1e-9) && Near(RoundLength(v, RoundMethod::Up, 0.025), 1.250, 1e-9) && Near(RoundLength(v, RoundMethod::Down, 0.025), 1.225, 1e-9), "1234.7 mm at 25 mm: nearest 1225, up 1250, down 1225");
  d8test::Check(Near(RoundLength(v, RoundMethod::None, 0.010), v, 1e-15) && Near(RoundLength(v, RoundMethod::Up, 0.0), v, 1e-15), "no method or no increment leaves the value alone");
  int bad = 0, checked = 0;
  const int incs[] = {5, 10, 25, 50, 100};
  for (int inc : incs)
    for (int mm = 0; mm <= 3000; ++mm) {
      const int nearest = ((2 * mm + inc) / (2 * inc)) * inc, up = ((mm + inc - 1) / inc) * inc, down = (mm / inc) * inc;
      const double x = mm / 1000.0, di = inc / 1000.0;
      bad += !Near(RoundLength(x, RoundMethod::Nearest, di), nearest / 1000.0, 1e-9);
      bad += !Near(RoundLength(x, RoundMethod::Up, di), up / 1000.0, 1e-9);
      bad += !Near(RoundLength(x, RoundMethod::Down, di), down / 1000.0, 1e-9);
      checked += 3;
    }
  d8test::Check(checked == 3 * 5 * 3001 && bad == 0, "all 45015 integer-millimetre cases (nearest, up, down at 5/10/25/50/100) agree with integer arithmetic");
  d8test::Check(Near(RoundLength(5.85, RoundMethod::Up, 0.05), 5.85, 1e-12) && Near(RoundLength(5.85, RoundMethod::Down, 0.05), 5.85, 1e-12), "a length that is already a multiple of the increment is unchanged by Up and Down");
}

// D8-REVIT-STRUCT-0062: "Bars in a set follow a layout rule: single, fixed number, ..., maximum spacing, number with spacing, or
// minimum clear spacing" (Rebar Set Layouts). Array length L is measured from the first to the last bar centre.
//   Single: 1 bar. Fixed number 7 over 3.0: spacing 3.0 / 6 = 0.5. Maximum spacing 0.2 over 3.0: 3.0 / 0.2 = 15 gaps -> 16 bars at 0.2;
//   over 3.05: 15.25 -> 16 gaps, 17 bars at 3.05 / 16 = 0.190625. Number with spacing 10 at 0.15: span 1.35. Minimum clear spacing
//   0.05 for d 0.016 over 3.0: pitch 0.066, 3.0 / 0.066 = 45.45 -> 45 gaps, 46 bars at 3.0 / 45 = 0.0666667 >= 0.066.
// Oracle: trial loops over the bar count (smallest count whose spacing is within the limit; largest count that keeps the clearance).
D8_EXTRA_TEST(StructRebarLayoutRules) {
  BarLayoutInput in;
  in.rule = BarLayoutRule::Single;
  BarLayout s = ComputeBarLayout(in);
  d8test::Check(s.ok && s.count == 1 && s.span == 0.0, "single: one bar");
  in.rule = BarLayoutRule::FixedNumber; in.number = 7; in.array_length = 3.0;
  BarLayout f = ComputeBarLayout(in);
  d8test::Check(f.ok && f.count == 7 && Near(f.spacing, 0.5, 1e-12) && Near(f.span, 3.0, 1e-12), "fixed number 7 over 3.0: spacing 0.5");
  in.rule = BarLayoutRule::MaximumSpacing; in.spacing = 0.2; in.array_length = 3.0;
  BarLayout m = ComputeBarLayout(in);
  d8test::Check(m.ok && m.count == 16 && Near(m.spacing, 0.2, 1e-12), "maximum spacing 0.2 over 3.0: 16 bars at 0.2 (an exact multiple adds no bar)");
  in.array_length = 3.05;
  m = ComputeBarLayout(in);
  d8test::Check(m.count == 17 && Near(m.spacing, 3.05 / 16.0, 1e-12) && m.spacing <= 0.2, "maximum spacing 0.2 over 3.05: 17 bars at 0.190625");
  in.rule = BarLayoutRule::NumberWithSpacing; in.number = 10; in.spacing = 0.15;
  BarLayout n = ComputeBarLayout(in);
  d8test::Check(n.ok && n.count == 10 && Near(n.spacing, 0.15, 1e-15) && Near(n.span, 1.35, 1e-12), "number with spacing: 10 bars at 0.15 span 1.35");
  in.rule = BarLayoutRule::MinimumClearSpacing; in.bar_diameter = 0.016; in.min_clear = 0.05; in.array_length = 3.0;
  BarLayout c = ComputeBarLayout(in);
  d8test::Check(c.ok && c.count == 46 && Near(c.spacing, 3.0 / 45.0, 1e-12) && c.spacing - 0.016 >= 0.05, "minimum clear spacing 0.05 for d 0.016 over 3.0: 46 bars, clear gap 0.0507 >= 0.05");
  in.rule = BarLayoutRule::MaximumSpacing; in.spacing = 0.0;
  d8test::Check(!ComputeBarLayout(in).ok, "a maximum spacing of zero is rejected");
  in.rule = BarLayoutRule::FixedNumber; in.number = 0;
  d8test::Check(!ComputeBarLayout(in).ok, "a fixed number of zero is rejected");

  int bad = 0, cases = 0;
  for (int li = 1; li <= 60; ++li)
    for (int si = 1; si <= 40; ++si) {
      const double L = li * 0.05, s = si * 0.01;
      BarLayoutInput a;
      a.rule = BarLayoutRule::MaximumSpacing; a.array_length = L; a.spacing = s;
      BarLayout got = ComputeBarLayout(a);
      int n_trial = 2;
      while (L / (n_trial - 1) > s + 1e-9) ++n_trial;  // smallest count whose spacing is within the limit
      bad += !(got.count == n_trial && got.spacing <= s + 1e-9);
      BarLayoutInput b;
      b.rule = BarLayoutRule::MinimumClearSpacing; b.array_length = L; b.bar_diameter = 0.01; b.min_clear = s;
      BarLayout gotb = ComputeBarLayout(b);
      int nb = 1;
      while (L / nb >= 0.01 + s - 1e-9) ++nb;  // one more gap while the spacing still keeps the clearance
      bad += !(gotb.count == nb + 0 && (gotb.count < 2 || gotb.spacing >= 0.01 + s - 1e-9));
      cases += 2;
    }
  d8test::Check(cases == 4800 && bad == 0, "4800 trial-loop comparisons for maximum spacing and minimum clear spacing agree");
}

// D8-REVIT-STRUCT-0082: "Take off concrete volumes by element, level and material" (Material Takeoff Schedules).
// Hand volumes: slab 6 x 5 x 0.2 = 6.0; column 0.4 x 0.4 x 3.0 = 0.48; beam 0.3 x 0.5 x 6.0 = 0.9; timber post 0.2 x 0.2 x 2.5 = 0.1.
//   Level 1: C30 slab 6.0 + column 0.48 + beam 0.9 = 7.38 (3 items), Timber 0.1 (1). Level 2: C30 slab 6.0 + column 0.48 = 6.48 (2),
//   C25 footing-like block 2 x 2 x 0.5 = 2.0 (1). Level totals: L1 7.48 (4), L2 8.48 (3). Material totals: C30 13.86 (5),
//   Timber 0.1 (1), C25 2.0 (1). Grand total 15.96 (7). The order of the elements must not matter: all 5040 permutations agree.
D8_EXTRA_TEST(StructConcreteTakeoffByLevelMaterial) {
  std::vector<TakeoffItem> items = {{"L1", "C30", 6 * 5 * 0.2}, {"L1", "C30", 0.4 * 0.4 * 3.0}, {"L1", "C30", 0.3 * 0.5 * 6.0}, {"L1", "Timber", 0.2 * 0.2 * 2.5},
                                    {"L2", "C30", 6 * 5 * 0.2}, {"L2", "C30", 0.4 * 0.4 * 3.0}, {"L2", "C25", 2 * 2 * 0.5}};
  TakeoffResult r = TakeOffByLevelMaterial(items);
  d8test::Check(r.rows.size() == 4 && r.level_totals.size() == 2 && r.material_totals.size() == 3, "4 level/material rows, 2 level totals, 3 material totals");
  d8test::Check(r.rows[0].level == "L1" && r.rows[0].material == "C30" && r.rows[0].count == 3 && Near(r.rows[0].volume, 7.38, 1e-12), "L1 / C30: 3 items, 7.38 m^3");
  d8test::Check(r.rows[1].level == "L1" && r.rows[1].material == "Timber" && r.rows[1].count == 1 && Near(r.rows[1].volume, 0.1, 1e-12), "L1 / Timber: 1 item, 0.1 m^3");
  d8test::Check(r.rows[2].level == "L2" && r.rows[2].material == "C25" && Near(r.rows[2].volume, 2.0, 1e-12), "L2 / C25: 2.0 m^3");
  d8test::Check(r.rows[3].level == "L2" && r.rows[3].material == "C30" && r.rows[3].count == 2 && Near(r.rows[3].volume, 6.48, 1e-12), "L2 / C30: 2 items, 6.48 m^3");
  d8test::Check(Near(r.level_totals[0].volume, 7.48, 1e-12) && r.level_totals[0].count == 4 && Near(r.level_totals[1].volume, 8.48, 1e-12) && r.level_totals[1].count == 3, "level totals 7.48 (4) and 8.48 (3)");
  d8test::Check(r.material_totals[0].material == "C25" && Near(r.material_totals[0].volume, 2.0, 1e-12) && r.material_totals[1].material == "C30" && Near(r.material_totals[1].volume, 13.86, 1e-12) && r.material_totals[2].material == "Timber", "material totals C25 2.0, C30 13.86, Timber 0.1");
  d8test::Check(Near(r.total_volume, 15.96, 1e-12) && r.total_count == 7, "grand total 15.96 m^3 over 7 items");
  std::vector<int> idx(items.size());
  std::iota(idx.begin(), idx.end(), 0);
  int bad = 0, perms = 0;
  do {
    std::vector<TakeoffItem> p;
    for (int k : idx) p.push_back(items[k]);
    TakeoffResult q = TakeOffByLevelMaterial(p);
    bool same = q.rows.size() == r.rows.size() && Near(q.total_volume, r.total_volume, 1e-12);
    for (size_t k = 0; same && k < q.rows.size(); ++k) same = q.rows[k].level == r.rows[k].level && q.rows[k].material == r.rows[k].material && q.rows[k].count == r.rows[k].count && Near(q.rows[k].volume, r.rows[k].volume, 1e-12);
    bad += !same;
    ++perms;
  } while (std::next_permutation(idx.begin(), idx.end()));
  d8test::Check(perms == 5040 && bad == 0, "all 5040 orderings of the elements give the same rows and totals");
  TakeoffResult none = TakeOffByLevelMaterial({});
  d8test::Check(none.rows.empty() && none.total_volume == 0.0 && none.total_count == 0, "an empty schedule has no rows and a zero total");
  d8test::Check(Near(RectSection(0.3, 0.5).area * 6.0, 0.9, 1e-12) && Near(RectSection(0.4, 0.4).area * 3.0, 0.48, 1e-12), "element volumes from the section area times the length: beam 0.9, column 0.48");
}

// D8-REVIT-STRUCT-0080: "A structural framing schedule is an ordinary category schedule whose fields come from beam properties such
// as cut length, volume, section type and start/end level offsets" (Create a Schedule or Quantity).
// Beam 6.0 long, extended 0.05 at the start, cut back 0.15 at the start and 0.25 at the end: cut length 6.0 + 0.05 - 0.15 - 0.25 = 5.65;
// section 0.3 x 0.5 = 0.15 m^2 -> volume 0.8475. Cutbacks that exceed the length give zero. Two beams listed together sum.
D8_EXTRA_TEST(StructFramingScheduleQuantities) {
  FramingMember b;
  b.length = 6.0; b.start_extension = 0.05; b.start_cutback = 0.15; b.end_cutback = 0.25;
  b.section_area = RectSection(0.3, 0.5).area;
  FramingQuantities q = ComputeFraming(b);
  d8test::Check(Near(q.cut_length, 5.65, 1e-12) && Near(q.volume, 0.8475, 1e-12), "cut length 5.65, volume 0.8475");
  FramingMember plain;
  plain.length = 4.0; plain.section_area = 0.02;
  d8test::Check(Near(ComputeFraming(plain).cut_length, 4.0, 1e-15) && Near(ComputeFraming(plain).volume, 0.08, 1e-15), "no joins: cut length equals the modelled length, volume 0.08");
  FramingMember over;
  over.length = 1.0; over.start_cutback = 0.7; over.end_cutback = 0.6; over.section_area = 0.05;
  d8test::Check(ComputeFraming(over).cut_length == 0.0 && ComputeFraming(over).volume == 0.0, "cutbacks longer than the member give zero, never negative");
  FramingMember ext;
  ext.length = 5.0; ext.start_extension = 0.1; ext.end_extension = 0.2; ext.section_area = 0.01;
  d8test::Check(Near(ComputeFraming(ext).cut_length, 5.3, 1e-12) && Near(ComputeFraming(ext).volume, 0.053, 1e-12), "extensions lengthen the cut length: 5.3, volume 0.053");
  d8test::Check(Near(ComputeFraming(b).volume + ComputeFraming(ext).volume, 0.8475 + 0.053, 1e-12), "a schedule total is the sum of the member volumes");
}

// D8-ARCHICAD-STRUCT-0017: "Generate surface live loads on zones by mapping zone classifications to load categories that hold unit
// values, and update them when zone geometry, classification or mapping changes" (Structural Loads > Create/Update Live Loads).
// Mapping Office -> Cat B 3.0, Corridor -> Cat C3 4.0, Storage -> Cat E 7.5 (kN/m^2); zones Office 40 m^2 -> 120, Corridor 12.5 -> 50,
// Storage 20 -> 150, Roof terrace 10 unmapped -> 0; total 320 and one unmapped zone. Update: corridor grows to 15 m^2 -> 60; the Cat B
// value changes to 3.5 -> 140; a reclassified zone (Office -> Storage) takes the Cat E value.
D8_EXTRA_TEST(StructZoneLiveLoads) {
  std::vector<LoadCategory> map = {{"Office", "Cat B", 3.0}, {"Corridor", "Cat C3", 4.0}, {"Storage", "Cat E", 7.5}};
  std::vector<ZoneLoadInput> zones = {{"Office", 40.0}, {"Corridor", 12.5}, {"Storage", 20.0}, {"Roof terrace", 10.0}};
  ZoneLoadsResult r = GenerateZoneLiveLoads(zones, map);
  d8test::Check(r.zones.size() == 4 && r.zones[0].mapped && r.zones[0].category == "Cat B" && Near(r.zones[0].load, 120.0, 1e-12), "Office 40 m^2 x 3.0 = 120 (Cat B)");
  d8test::Check(Near(r.zones[1].load, 50.0, 1e-12) && Near(r.zones[2].load, 150.0, 1e-12), "Corridor 12.5 x 4.0 = 50, Storage 20 x 7.5 = 150");
  d8test::Check(!r.zones[3].mapped && r.zones[3].load == 0.0 && r.unmapped == 1, "a classification with no category gets no load and is counted as unmapped");
  d8test::Check(Near(r.total_load, 320.0, 1e-12), "total surface live load 320 kN");
  zones[1].area = 15.0;
  d8test::Check(Near(GenerateZoneLiveLoads(zones, map).zones[1].load, 60.0, 1e-12), "updating the zone geometry: corridor 15 m^2 -> 60");
  map[0].unit_value = 3.5;
  d8test::Check(Near(GenerateZoneLiveLoads(zones, map).zones[0].load, 140.0, 1e-12), "updating the mapping: Cat B 3.5 -> office 140");
  zones[0].classification = "Storage";
  ZoneLoadsResult u = GenerateZoneLiveLoads(zones, map);
  d8test::Check(u.zones[0].category == "Cat E" && Near(u.zones[0].load, 300.0, 1e-12), "updating the classification: office 40 m^2 reclassified Storage -> 300");
  d8test::Check(Near(u.total_load, 300.0 + 60.0 + 150.0, 1e-12) && u.unmapped == 1, "total after the updates 510");
  zones[2].area = -5.0;
  d8test::Check(GenerateZoneLiveLoads(zones, map).zones[2].load == 0.0, "a negative area never produces a load");
}
