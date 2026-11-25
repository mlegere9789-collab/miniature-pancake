// Tests of include/dino8/kernel/forma_site.h: ground grids (spacing, validity mask, circle), plot constraints, level stacks, area program
// and unit mix, building functions and street sections with curb returns (Register rows D8-FORMA-ANALYSIS-0028, 0029, 0039, 0041,
// SITE-0009, 0015, 0016, STREET-0001, 0002, BLDG-0003). Every expected value is derived by hand in the comment of its test or comes from
// a published integer sequence (the Gauss circle counts, OEIS A000328), never from the code under test.
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/forma_site.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::forma;

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
std::vector<P2> Rect(double x0, double y0, double x1, double y1) { return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}; }
P2 Rot(P2 p, double deg) {
  const double c = std::cos(deg * kPi / 180), s = std::sin(deg * kPi / 180);
  return {p.x * c - p.y * s, p.x * s + p.y * c};
}
std::vector<P2> RotAll(const std::vector<P2>& v, double deg) {
  std::vector<P2> o;
  for (const P2& p : v) o.push_back(Rot(p, deg));
  return o;
}

// ---- ANALYSIS-0039 ---------------------------------------------------------------------------------------------------------
// Grid spacing is the caller's choice. Point counts: a 50 x 50 m region from (0, 0) has floor(50 / s) + 1 points per side: s = 5 -> 11 x 11
// = 121, s = 2.5 -> 21 x 21 = 441, s = 0.5 -> 101 x 101 = 10201, s = 3 -> 17 x 17 = 289 (the last point at 48). Convergence on the box:
// a 10 x 10 m footprint, height 10, sun az 210, el 45: shadow shift v = h / tan(el) = 10 m towards az 30, v = (5, 8.660); the shadow outside
// the footprint has area |vx| b + |vy| a = 50 + 86.603 = 136.603 m2. The shaded points off the footprint, times s^2, estimate it: within
// 2 % at 0.5 m (the row's tolerance), and the error at 0.25 m is smaller than at 2 m.
D8_EXTRA_TEST(Rw5FormaGridSpacing) {
  d8test::Check(MakeGroundGrid(0, 0, 50, 50, 5).Count() == 121 && MakeGroundGrid(0, 0, 50, 50, 2.5).Count() == 441 &&
                    MakeGroundGrid(0, 0, 50, 50, 0.5).Count() == 10201 && MakeGroundGrid(0, 0, 50, 50, 3).Count() == 289,
                "grid spacing: 121, 441, 10201 and 289 points for spacing 5, 2.5, 0.5 and 3 over 50 x 50 m");
  const GroundGrid g3 = MakeGroundGrid(0, 0, 50, 50, 3);
  d8test::Check(g3.nx == 17 && Near(g3.Point(16, 16).x, 48, 1e-12) && Near(g3.Point(1, 2).y, 6, 1e-12), "grid spacing: the last point at spacing 3 is 48 and point (1, 2) is at y = 6");
  const std::vector<Tri> box = PrismTriangles(Rect(0, 0, 10, 10), 0, 10);
  SunSample s;
  s.dir = DirFromAzEl(210, 45);
  const std::vector<SunSample> one = {s};
  const double expected = 5.0 * 10 + 8.660254037844386 * 10;
  d8test::Check(Near(expected, 136.603, 1e-3), "grid convergence: the closed-form shadow area is 136.603 m2");
  double err[4];
  const double spacings[4] = {2.0, 1.0, 0.5, 0.25};
  for (int k = 0; k < 4; ++k) {
    GroundGrid g = MakeGroundGrid(-10.13, -10.07, 40.0, 40.0, spacings[k]);
    MaskGrid(g, {Rect(0, 0, 10, 10)}, nullptr);
    int shaded = 0;
    for (int j = 0; j < g.ny; ++j)
      for (int i = 0; i < g.nx; ++i) {
        if (!g.Valid(i, j)) continue;
        const P2 p = g.Point(i, j);
        if (SunHoursAt({p.x, p.y, 0}, {0, 0, 0}, box, one, 1, 1).totalHours < 0.5) ++shaded;
      }
    err[k] = std::fabs(shaded * spacings[k] * spacings[k] - expected) / expected;
  }
  d8test::Check(err[2] < 0.02, "grid convergence: the 0.5 m grid is within 2 % of the closed-form shadow area");
  d8test::Check(err[3] < err[0], "grid convergence: the 0.25 m grid is closer to the closed form than the 2 m grid");
  d8test::Check(err[0] > 0.0005, "grid convergence decoy: the 2 m grid does not already reproduce the closed form exactly");
}

// ---- ANALYSIS-0041 ---------------------------------------------------------------------------------------------------------
// Validity mask. Site: an L-shape, the 100 x 60 rectangle without its corner [60,100] x [30,60]: vertices (0,0) (100,0) (100,30) (60,30)
// (60,60) (0,60). Grid origin (0,0), spacing 5: 21 x 13 = 273 points. Building [20,40] x [10,20]: the points x in {20,25,30,35,40} and
// y in {10,15,20} are inside or on it: 5 x 3 = 15 invalid. Outside the L: x in {65 .. 100} (8 values) and y in {35 .. 60} (6 values) = 48
// invalid (points on the boundary x = 60 or y = 30 are inside). Valid: 273 - 15 - 48 = 210. A point is invalid exactly when the closed
// predicate (in the building box) or (in the removed corner, boundary excluded) holds; origin and spacing are reported.
D8_EXTRA_TEST(Rw5FormaGridValidityMask) {
  GroundGrid g = MakeGroundGrid(0, 0, 100, 60, 5);
  d8test::Check(g.Count() == 273 && g.nx == 21 && g.ny == 13 && Near(g.x0, 0, 0) && Near(g.y0, 0, 0) && Near(g.spacing, 5, 0), "mask: 21 x 13 = 273 points from the origin (0, 0) at spacing 5");
  const std::vector<P2> site = {{0, 0}, {100, 0}, {100, 30}, {60, 30}, {60, 60}, {0, 60}};
  MaskGrid(g, {Rect(20, 10, 40, 20)}, &site);
  d8test::Check(g.ValidCount() == 210, "mask: 273 points less 15 under the building and 48 outside the L site leave 210 valid");
  int wrong = 0;
  for (int j = 0; j < g.ny; ++j)
    for (int i = 0; i < g.nx; ++i) {
      const double x = 5.0 * i, y = 5.0 * j;
      const bool underBuilding = x >= 20 && x <= 40 && y >= 10 && y <= 20;
      const bool outsideSite = x > 60 && y > 30;
      if (g.Valid(i, j) == (underBuilding || outsideSite)) ++wrong;
    }
  d8test::Check(wrong == 0, "mask: a point is invalid exactly under the footprint (boundary included) or outside the site");
  GroundGrid g2 = MakeGroundGrid(0, 0, 100, 60, 5);
  MaskGrid(g2, {Rect(20, 10, 40, 20)}, nullptr);
  d8test::Check(g2.ValidCount() == 258, "mask: without a site polygon only the 15 points under the building are invalid (258 valid)");
  GroundGrid g3 = MakeGroundGrid(0, 0, 100, 60, 5);
  MaskGrid(g3, {}, nullptr);
  d8test::Check(g3.ValidCount() == 273, "mask decoy: with no building every point is valid, so the 15 invalid points above are the footprint's doing");
}

// ---- SITE-0016 -------------------------------------------------------------------------------------------------------------
// A circle bounds the analysis area. Grid spacing 1 over [-30,30]^2 with the circle centred on a grid point: the points inside radius r
// are the Gauss circle counts (OEIS A000328): r = 1 .. 5, 10, 20 give 5, 13, 29, 49, 81, 317, 1257. Their ratio to pi r^2 (the row's
// sketch) is within 1 % for r >= 10. A centre moved by whole steps keeps the count; centre (0.5, 0.5) gives 80 (r = 5), 32 (r = 3) and
// 316 (r = 10) by an independent brute-force enumeration done by hand-run script; centre (0.3, 0.2) with r = 5 gives 79. Combined with a
// building mask the valid points shrink by the points under the footprint inside the circle.
D8_EXTRA_TEST(Rw5FormaAnalysisCircle) {
  auto count = [](double cx, double cy, double r) {
    GroundGrid g = MakeGroundGrid(-30, -30, 30, 30, 1);
    ClipGridToCircle(g, {cx, cy, r});
    return g.ValidCount();
  };
  d8test::Check(count(0, 0, 1) == 5 && count(0, 0, 2) == 13 && count(0, 0, 3) == 29 && count(0, 0, 4) == 49 && count(0, 0, 5) == 81 && count(0, 0, 10) == 317 && count(0, 0, 20) == 1257,
                "circle: radius 1 to 5, 10 and 20 give the Gauss circle counts 5, 13, 29, 49, 81, 317, 1257");
  d8test::Check(Near(count(0, 0, 10) / (kPi * 100.0), 1.0, 0.01) && Near(count(0, 0, 20) / (kPi * 400.0), 1.0, 0.01), "circle: the count over pi r^2 / cell area is within 1 % for radius 10 and 20");
  d8test::Check(count(7, -4, 5) == 81 && count(-12, 9, 10) == 317, "circle: moving the centre by whole steps keeps the count");
  d8test::Check(count(0.5, 0.5, 5) == 80 && count(0.5, 0.5, 3) == 32 && count(0.5, 0.5, 10) == 316 && count(0.3, 0.2, 5) == 79, "circle: centres off the grid points give 80, 32, 316 and 79 (brute-force enumeration)");
  d8test::Check(count(0, 0, 5) != count(0, 0, 4) && count(0, 0, 5) != 80, "circle decoy: radius changes the count, and the boundary points are included (81, not 80)");
  GroundGrid g = MakeGroundGrid(-30, -30, 30, 30, 1);
  MaskGrid(g, {Rect(1, 1, 3, 2)}, nullptr);   // 3 x 2 = 6 grid points inside or on the footprint, all inside the circle of radius 5
  ClipGridToCircle(g, {0, 0, 5});
  d8test::Check(g.ValidCount() == 81 - 6, "circle: a building footprint (6 points) inside the circle removes its points from the valid 81");
}

// ---- SITE-0015 -------------------------------------------------------------------------------------------------------------
// Plot constraints. Plot [0,40] x [0,60] = 2400 m2, setbacks per edge: south (edge 0) 5, east (1) 3, north (2) 6, west (3) 3; height limit
// 30; coverage limit 0.30; area ratio limit 1.5.
//  A [3,20] x [5,25]: west 3 (= limit), south 5 (= limit): complies; height 12.  B [10,30] x [4,20]: 4 m from the south edge: setback
//  violation of edge 0 with value 4 and limit 5.  C [10,30] x [30,50], height 40: height violation (40 over 30); north gap 10 fine.
//  D [35,45] x [20,30]: sticks out of the plot: "outside" and no setback report.
// Coverage of A and C: (17 x 20 + 20 x 20) / 2400 = 740 / 2400 = 0.30833 > 0.30: violation. A alone: 0.14167, none. GFA A = 340 x 3 = 1020,
// C = 400 x 10 = 4000: ratio (1020 + 4000) / 2400 = 2.0917 > 1.5: violation; A alone 0.425 fine.
// Rotating the plot and the buildings by 30 degrees leaves every distance and verdict the same. An L-shaped plot (0,0) (40,0) (40,20)
// (20,20) (20,40) (0,40) with a setback of 5 everywhere: a building [8,17] x [8,17] is 3 sqrt(2) = 4.2426 from the re-entrant corner (20,20):
// violations of edges 2 and 3 with that value.
D8_EXTRA_TEST(Rw5FormaPlotConstraints) {
  Plot plot;
  plot.id = "P1";
  plot.boundary = Rect(0, 0, 40, 60);
  plot.setback = {5, 3, 6, 3};
  plot.maxHeight = 30;
  plot.maxCoverage = 0.30;
  plot.maxAreaRatio = 1.5;
  const PlotBuilding A{"A", Rect(3, 5, 20, 25), 12, 1020}, B{"B", Rect(10, 4, 30, 20), 12, 100}, C{"C", Rect(10, 30, 30, 50), 40, 4000}, D{"D", Rect(35, 20, 45, 30), 10, 100};
  auto find = [](const std::vector<Violation>& v, const std::string& b, const std::string& rule) -> const Violation* {
    for (const Violation& x : v)
      if (x.building == b && x.rule == rule) return &x;
    return nullptr;
  };
  plot.maxAreaRatio = 0;
  plot.maxCoverage = 0;
  const std::vector<Violation> vA = CheckPlot(plot, {A});
  d8test::Check(vA.empty(), "constraints: a building exactly on its setback lines complies (3 m west, 5 m south)");
  const std::vector<Violation> vB = CheckPlot(plot, {B});
  d8test::Check(vB.size() == 1 && find(vB, "B", "setback") && find(vB, "B", "setback")->edge == 0 && Near(find(vB, "B", "setback")->value, 4, 1e-9) && Near(find(vB, "B", "setback")->limit, 5, 1e-9),
                "constraints: B 4 m from the south edge breaks the 5 m setback of edge 0");
  const std::vector<Violation> vC = CheckPlot(plot, {C});
  d8test::Check(vC.size() == 1 && find(vC, "C", "height") && Near(find(vC, "C", "height")->value, 40, 1e-9) && Near(find(vC, "C", "height")->limit, 30, 1e-9), "constraints: C at 40 m breaks the 30 m height limit and nothing else");
  const std::vector<Violation> vD = CheckPlot(plot, {D});
  d8test::Check(vD.size() == 1 && find(vD, "D", "outside") && !find(vD, "D", "setback"), "constraints: D outside the plot is reported as outside only");
  plot.maxCoverage = 0.30;
  plot.maxAreaRatio = 1.5;
  const std::vector<Violation> vAC = CheckPlot(plot, {A, C});
  d8test::Check(find(vAC, "", "coverage") && Near(find(vAC, "", "coverage")->value, 740.0 / 2400.0, 1e-12) && find(vAC, "", "area_ratio") && Near(find(vAC, "", "area_ratio")->value, 5020.0 / 2400.0, 1e-12) &&
                    find(vAC, "C", "height"),
                "constraints: A and C together cover 740 / 2400 = 0.3083 (over 0.30) and have area ratio 2.0917 (over 1.5)");
  d8test::Check(CheckPlot(plot, {A}).empty(), "constraints: A alone is within coverage 0.1417 and area ratio 0.425");
  // rotated by 30 degrees
  Plot rp = plot;
  rp.boundary = RotAll(plot.boundary, 30);
  std::vector<PlotBuilding> rb = {A, B, C};
  for (PlotBuilding& b : rb) b.footprint = RotAll(b.footprint, 30);
  const std::vector<Violation> vr = CheckPlot(rp, rb);
  d8test::Check(find(vr, "B", "setback") && Near(find(vr, "B", "setback")->value, 4, 1e-9) && find(vr, "B", "setback")->edge == 0 && find(vr, "C", "height") && !find(vr, "A", "setback") && !find(vr, "A", "outside"),
                "constraints: the same plot and buildings rotated by 30 degrees give the same violations and distances");
  Plot lp;
  lp.id = "L";
  lp.boundary = {{0, 0}, {40, 0}, {40, 20}, {20, 20}, {20, 40}, {0, 40}};
  lp.setback = {5};
  const std::vector<Violation> vl = CheckPlot(lp, {{"E", Rect(8, 8, 17, 17), 10, 100}});
  d8test::Check(vl.size() == 2 && vl[0].edge == 2 && vl[1].edge == 3 && Near(vl[0].value, 3 * std::sqrt(2.0), 1e-9) && Near(vl[1].value, 3 * std::sqrt(2.0), 1e-9),
                "constraints: on an L-shaped plot a building 3 sqrt(2) = 4.2426 m from the re-entrant corner breaks the 5 m setback of both edges meeting there");
  d8test::Check(CheckPlot(lp, {{"F", Rect(8, 8, 14, 20), 10, 100}}).empty(), "constraints: [8,14] x [8,20] is 6 m from the re-entrant corner (14,20) to (20,20) and 8 m from the other edges: complies");
  const std::vector<Violation> vl2 = CheckPlot(lp, {{"G", Rect(8, 8, 16, 20), 10, 100}});
  d8test::Check(vl2.size() == 2 && Near(vl2[0].value, 4.0, 1e-9) && Near(vl2[1].value, 4.0, 1e-9), "constraints: widening it to x = 16 brings it to 4 m from the corner: both edges report 4 m");
}

// ---- BLDG-0003 -------------------------------------------------------------------------------------------------------------
// Level stack. Footprint 40 x 20 = 800 m2. Ground floor 4.5 m (retail), nine typical floors of 3.2 m (residential), two basements of 3.0 m
// (parking, the first directly below the ground floor). Elevations of the floor slabs: basement -2 at -6.0, -1 at -3.0, ground 0, level 1
// at 4.5, level k at 4.5 + (k - 1) 3.2, so level 9 at 30.1; top = 30.1 + 3.2 = 33.3. Twelve levels, each 800 m2 (basements may have their
// own outline: a 30 x 20 = 600 m2 deeper basement). The first floor's elevation follows `ground` offset rules: relative to the ground floor.
D8_EXTRA_TEST(Rw5FormaLevelStack) {
  BuildingSpec b;
  b.id = "B1";
  b.footprint = Rect(0, 0, 40, 20);
  b.floors = {{4.5, 1, "retail", {}}, {3.2, 9, "residential", {}}};
  b.basements = {{3.0, 1, "parking", {}}, {3.0, 1, "parking", Rect(0, 0, 30, 20)}};
  const std::vector<Level> lv = BuildLevels(b);
  d8test::Check(lv.size() == 12 && lv.front().index == -2 && lv.back().index == 9 && lv[2].index == 0, "levels: twelve levels from basement -2 to level 9, the ground floor third");
  d8test::Check(Near(lv[0].elevation, -6.0, 1e-9) && Near(lv[1].elevation, -3.0, 1e-9) && Near(lv[2].elevation, 0.0, 1e-9) && Near(lv[3].elevation, 4.5, 1e-9) && Near(lv[4].elevation, 7.7, 1e-9) &&
                    Near(lv[11].elevation, 30.1, 1e-9),
                "levels: slab elevations -6, -3, 0, 4.5, 7.7 ... 30.1");
  d8test::Check(Near(BuildingTopElevation(lv), 33.3, 1e-9), "levels: the top is 33.3 m (30.1 + 3.2)");
  d8test::Check(Near(lv[0].area, 600, 1e-9) && Near(lv[1].area, 800, 1e-9) && Near(lv[5].area, 800, 1e-9) && lv[0].basement && !lv[2].basement && lv[0].function == "parking" && lv[3].function == "residential",
                "levels: area per level 800 m2, the deepest basement has its own 600 m2 outline; functions and the basement flag are kept");
  double sum = 0, below = 0;
  for (const Level& l : lv) {
    sum += l.area;
    if (l.basement) below += l.area;
  }
  d8test::Check(Near(sum, 600 + 800 * 11, 1e-9) && Near(below, 1400, 1e-9), "levels: total area 9400 m2 with 1400 m2 below ground");
  d8test::Check(!Near(BuildingTopElevation(lv), 9 * 3.2 + 4.5 + 6.0, 1e-9), "levels decoy: the top is not measured from the deepest basement floor (39.3)");
}

// ---- ANALYSIS-0028 and SITE-0009 -------------------------------------------------------------------------------------------
// Area program of a proposal on a 100 x 80 = 8000 m2 site.
//  B1: footprint 40 x 20 = 800: basements 2 x parking, ground floor retail, nine floors of residential: gfa retail 800, residential 7200,
//      parking 1600 (below ground), above ground 8000. 120 units: 40 of 45 m2, 60 of 70 m2, 20 of 95 m2; 100 parking spots, 1600 m2.
//  B2: an L-shaped footprint, a 30 x 30 square without its 10 x 10 corner = 800, six office floors: 4800.
// Totals: GFA 9600 + 4800 = 14400 (above 12800, below 1600); coverage (800 + 800) / 8000 = 0.2; units 120; parking 100 spots. Zones: landscape
// 1000 m2, paving 500. Custom metric gfa per unit 14400 / 120 = 120; a custom ratio with a zero denominator is 0.
// SITE-0009: functions carry a name and a colour; totals per function in the order of the definitions with share of the total GFA:
// residential (0x2E7D32) 7200 = 50 %, office (0x1565C0) 4800 = 33.33 %, retail (0xE65100) 800 = 5.56 %, parking (0x616161) 1600 = 11.11 %.
D8_EXTRA_TEST(Rw5FormaAreaProgram) {
  BuildingSpec b1;
  b1.id = "B1";
  b1.footprint = Rect(0, 0, 40, 20);
  b1.floors = {{4.5, 1, "retail", {}}, {3.2, 9, "residential", {}}};
  b1.basements = {{3.0, 2, "parking", {}}};
  b1.units = {{"1BR", 45, 40}, {"2BR", 70, 60}, {"3BR", 95, 20}};
  b1.parkingSpots = 100;
  b1.parkingArea = 1600;
  BuildingSpec b2;
  b2.id = "B2";
  b2.footprint = {{50, 0}, {80, 0}, {80, 20}, {70, 20}, {70, 30}, {50, 30}};
  b2.floors = {{4.0, 6, "office", {}}};
  const std::vector<Zone> zones = {{"landscape", Rect(0, 40, 50, 60)}, {"paving", Rect(60, 40, 80, 65)}};
  const Metrics m = ProposalMetrics(Rect(0, 0, 100, 80), {b1, b2}, zones);
  d8test::Check(Near(m.siteArea, 8000, 1e-9) && Near(m.groundFootprint, 1600, 1e-9) && Near(m.coverage, 0.2, 1e-12), "program: site 8000 m2, footprint 1600 m2, coverage 0.2");
  d8test::Check(Near(m.gfaTotal, 14400, 1e-9) && Near(m.gfaAbove, 12800, 1e-9) && Near(m.gfaBelow, 1600, 1e-9), "program: GFA 14400 m2 (12800 above ground, 1600 below)");
  d8test::Check(Near(m.gfaByFunction.at("residential"), 7200, 1e-9) && Near(m.gfaByFunction.at("office"), 4800, 1e-9) && Near(m.gfaByFunction.at("retail"), 800, 1e-9) && Near(m.gfaByFunction.at("parking"), 1600, 1e-9) &&
                    m.gfaByFunction.size() == 4,
                "program: GFA by function residential 7200, office 4800, retail 800, parking 1600");
  d8test::Check(m.unitCount == 120 && m.parkingSpots == 100 && Near(m.parkingArea, 1600, 1e-9) && Near(m.zoneAreaByType.at("landscape"), 1000, 1e-9) && Near(m.zoneAreaByType.at("paving"), 500, 1e-9),
                "program: 120 units, 100 parking spots on 1600 m2, zones landscape 1000 m2 and paving 500 m2");
  d8test::Check(Near(CustomRatio(m, "gfa", "units"), 120.0, 1e-12) && CustomRatio(m, "gfa", "nothing") == 0.0 && Near(CustomRatio(m, "parking_area", "parking_spots"), 16.0, 1e-12),
                "program: custom ratios, GFA per unit 120 and parking area per spot 16 m2");
  d8test::Check(!Near(m.gfaTotal, 12800, 1.0) && !Near(m.coverage, 14400.0 / 8000.0, 0.01), "program decoy: GFA without the basements (12800) and coverage counting every floor (1.8) are different numbers");
  const std::vector<FunctionDef> defs = {{"residential", 0x2E7D32}, {"office", 0x1565C0}, {"retail", 0xE65100}, {"parking", 0x616161}};
  const std::vector<FunctionTotal> ft = FunctionTotals(m, defs);
  const double expectedArea[] = {7200, 4800, 800, 1600};
  const double expectedShare[] = {0.5, 4800.0 / 14400.0, 800.0 / 14400.0, 1600.0 / 14400.0};
  int bad = 0;
  for (int i = 0; i < 4; ++i)
    if (ft[i].name != defs[i].name || ft[i].rgb != defs[i].rgb || !Near(ft[i].area, expectedArea[i], 1e-9) || !Near(ft[i].share, expectedShare[i], 1e-12)) ++bad;
  d8test::Check(ft.size() == 4 && bad == 0 && ft[1].rgb == 0x1565C0, "functions: name, colour, area and share per function: residential 7200 (50 %), office 4800 (33.33 %), retail 800 (5.56 %), parking 1600 (11.11 %)");
  d8test::Check(Near(ft[0].share + ft[1].share + ft[2].share + ft[3].share, 1.0, 1e-12), "functions: the shares add to 100 %");
  const std::vector<FunctionTotal> ft2 = FunctionTotals(m, {{"residential", 0x2E7D32}, {"parking", 0x616161}, {"hotel", 0xAA00FF}});
  d8test::Check(ft2.size() == 4 && ft2[2].name == "hotel" && ft2[2].area == 0 && ft2[3].name == "(undefined)" && Near(ft2[3].area, 4800 + 800, 1e-9) && Near(ft2[3].share, 5600.0 / 14400.0, 1e-12),
                "functions: a defined function without floors has area 0; floors with an undefined function (office, retail: 5600 m2) are collected in an extra row");
}

// ---- ANALYSIS-0029 ---------------------------------------------------------------------------------------------------------
// Unit mix. B1: 40 x 1BR (45 m2), 60 x 2BR (70 m2), 20 x 3BR (95 m2): shares 1/3, 1/2, 1/6; mean sizes 45, 70, 95; against the target mix
// 0.30 / 0.50 / 0.20 deviations +0.03333, 0, -0.03333. B3 adds 20 x 1BR of 55 m2 and 20 x 2BR of 75 m2: the proposal has 60 x 1BR (mean
// (40 x 45 + 20 x 55) / 60 = 48.333), 80 x 2BR (mean (60 x 70 + 20 x 75) / 80 = 71.25), 20 x 3BR: 160 units, shares 0.375, 0.5, 0.125.
D8_EXTRA_TEST(Rw5FormaUnitMix) {
  BuildingSpec b1;
  b1.footprint = Rect(0, 0, 40, 20);
  b1.floors = {{3.0, 1, "residential", {}}};
  b1.units = {{"1BR", 45, 40}, {"2BR", 70, 60}, {"3BR", 95, 20}};
  BuildingSpec b3 = b1;
  b3.units = {{"1BR", 55, 20}, {"2BR", 75, 20}};
  const std::map<std::string, double> target = {{"1BR", 0.30}, {"2BR", 0.50}, {"3BR", 0.20}};
  const std::vector<MixRow> r1 = UnitMix(ProposalMetrics(Rect(0, 0, 100, 100), {b1}), target);
  d8test::Check(r1.size() == 3 && r1[0].type == "1BR" && r1[0].count == 40 && Near(r1[0].share, 1.0 / 3, 1e-12) && Near(r1[1].share, 0.5, 1e-12) && Near(r1[2].share, 1.0 / 6, 1e-12) &&
                    Near(r1[0].meanArea, 45, 1e-9) && Near(r1[1].meanArea, 70, 1e-9) && Near(r1[2].meanArea, 95, 1e-9),
                "unit mix: building B1 has 40, 60 and 20 units, shares 1/3, 1/2, 1/6 and mean sizes 45, 70, 95 m2");
  d8test::Check(Near(r1[0].deviation, 1.0 / 3 - 0.30, 1e-12) && Near(r1[1].deviation, 0, 1e-12) && Near(r1[2].deviation, 1.0 / 6 - 0.20, 1e-12), "unit mix: deviations from the target mix +0.0333, 0, -0.0333");
  const std::vector<MixRow> rp = UnitMix(ProposalMetrics(Rect(0, 0, 100, 100), {b1, b3}), target);
  d8test::Check(rp.size() == 3 && rp[0].count == 60 && rp[1].count == 80 && rp[2].count == 20 && Near(rp[0].share, 0.375, 1e-12) && Near(rp[1].share, 0.5, 1e-12) && Near(rp[2].share, 0.125, 1e-12) &&
                    Near(rp[0].meanArea, 48.0 + 1.0 / 3, 1e-9) && Near(rp[1].meanArea, 71.25, 1e-9),
                "unit mix: the proposal has 160 units, 60 / 80 / 20 by type, mean sizes 48.33 and 71.25 m2");
  double shares = 0;
  for (const MixRow& r : rp) shares += r.share;
  d8test::Check(Near(shares, 1.0, 1e-12) && !Near(rp[0].share, r1[0].share, 0.01), "unit mix decoy: shares add to 1 and the proposal mix differs from the single building's");
}

// ---- STREET-0001 -----------------------------------------------------------------------------------------------------------
// Street section: sidewalk 3.0, lane 3.5, lane 3.5, median 2.0, lane 3.5, lane 3.5, sidewalk 3.0 = 22.0 m wide, centred. Offsets from the
// centreline (left positive): sidewalk [8, 11], lanes [4.5, 8] and [1, 4.5], median [-1, 1], lanes [-4.5, -1] and [-8, -4.5], sidewalk
// [-11, -8]. Straight centreline of 100 m: each band is width x 100 (sidewalk 300, lane 350, median 200), total 2200 = 22 x 100.
// Centreline (0,0) -> (100,0) -> (100,100) turning left with mitred joins: a band [a, b] has area (b - a)(200 - a - b): left sidewalk 3 x
// (200 - 19) = 543, right sidewalk 3 x (200 + 19) = 657, lane [1, 4.5]: 3.5 x (200 - 5.5) = 680.75, median [-1, 1]: 2 x 200 = 400; the whole
// section 22 x 200 = 4400. Widening a lane by 0.5 m grows the section to 22.5 and the straight total to 2250.
D8_EXTRA_TEST(Rw5FormaStreetSection) {
  StreetSection s;
  s.strips = {{"sidewalk", 3.0}, {"lane", 3.5}, {"lane", 3.5}, {"median", 2.0}, {"lane", 3.5}, {"lane", 3.5}, {"sidewalk", 3.0}};
  d8test::Check(Near(s.Width(), 22.0, 1e-12), "street section: sidewalk, 4 lanes, median, sidewalk add to 22 m");
  const auto off = s.Offsets();
  d8test::Check(off.size() == 7 && Near(off[0].first, 8, 1e-12) && Near(off[0].second, 11, 1e-12) && Near(off[3].first, -1, 1e-12) && Near(off[3].second, 1, 1e-12) && Near(off[6].first, -11, 1e-12) &&
                    Near(off[6].second, -8, 1e-12),
                "street section: offsets run from +11 (left sidewalk) through the median [-1, 1] to -11");
  const std::vector<P2> line = {{0, 0}, {100, 0}};
  double total = 0;
  std::vector<double> areas;
  for (const auto& o : off) {
    areas.push_back(PolyArea(OffsetBand(line, o.first, o.second)));
    total += areas.back();
  }
  d8test::Check(Near(areas[0], 300, 1e-9) && Near(areas[1], 350, 1e-9) && Near(areas[3], 200, 1e-9) && Near(total, 2200, 1e-9 * 2200), "street section: along a straight 100 m centreline sidewalk 300, lane 350, median 200, total 2200 m2");
  const std::vector<P2> bent = {{0, 0}, {100, 0}, {100, 100}};
  const double leftWalk = PolyArea(OffsetBand(bent, 8, 11)), rightWalk = PolyArea(OffsetBand(bent, -11, -8)), lane = PolyArea(OffsetBand(bent, 1, 4.5)), median = PolyArea(OffsetBand(bent, -1, 1));
  d8test::Check(Near(leftWalk, 543, 1e-9) && Near(rightWalk, 657, 1e-9) && Near(lane, 680.75, 1e-9) && Near(median, 400, 1e-9),
                "street section: around a left turn the inside sidewalk is 543 m2, the outside one 657 m2, the lane [1, 4.5] 680.75 m2, the median 400 m2");
  double bentTotal = 0;
  for (const auto& o : off) bentTotal += PolyArea(OffsetBand(bent, o.first, o.second));
  d8test::Check(Near(bentTotal, 4400, 1e-9 * 4400), "street section: the whole section around the turn is 22 x 200 = 4400 m2");
  d8test::Check(!Near(leftWalk, 600, 1.0), "street section decoy: the inside band is shorter than the centreline length (543, not 3 x 200)");
  s.strips[1].width = 4.0;
  double w2 = 0;
  for (const auto& o : s.Offsets()) w2 += PolyArea(OffsetBand(line, o.first, o.second));
  d8test::Check(Near(s.Width(), 22.5, 1e-12) && Near(w2, 2250, 1e-9 * 2250), "street section: a 0.5 m wider lane gives 22.5 m and 2250 m2");
}

// ---- STREET-0002 -----------------------------------------------------------------------------------------------------------
// Intersection with curb returns. E-W street 14 m wide (x from -50 to 50), N-S street 10 m wide (y from -40 to 40), curb return radius 6 m.
// Cross shape area 2 L w1 + 2 M w2 - w1 w2 = 1400 + 800 - 140 = 2060. Each return adds r^2 (1 - pi/4) = 7.7257 m2 (the square corner minus the
// quarter disc): 4 returns 30.9027, exact total 2090.9027. With 64 chords per return the outline is a polygon; each chord adds the
// circular segment (r^2 / 2)(phi - sin phi), phi = pi / 128: the polygon area is exact + 4 x 64 x 18 x (phi - sin phi) = exact + 0.01135.
// Radius 0 is the plain cross, 2060. Arc vertices all lie on circles of radius r around the corner centres (hx + r, hy + r), ...
D8_EXTRA_TEST(Rw5FormaIntersectionCurbReturns) {
  const double w1 = 14, w2 = 10, L = 50, M = 40, r = 6;
  const int n = 64;
  const std::vector<P2> o = IntersectionOutline(w1, w2, L, M, r, n);
  const double exact = 2 * L * w1 + 2 * M * w2 - w1 * w2 + 4 * r * r * (1 - kPi / 4);
  const double phi = (kPi / 2) / n;
  const double chordExtra = 4.0 * n * (r * r / 2) * (phi - std::sin(phi));
  d8test::Check(Near(exact, 2090.9027, 1e-4) && Near(chordExtra, 0.01135, 2e-5), "curb return: the closed form is 2090.9027 m2 and the chord correction 0.01135 m2");
  d8test::Check(Near(PolyArea(o), exact + chordExtra, 1e-9), "curb return: the polygon area equals the closed form plus the circular segments of its chords (1e-9)");
  d8test::Check(Near(PolyArea(o), exact, 0.012), "curb return: within 0.012 m2 of the exact quarter-disc form");
  int offCircle = 0;
  const double hx = w2 / 2, hy = w1 / 2;
  for (const P2& p : o) {
    // vertices of the four arcs are those not on the straight street edges; test the NE return centre only for points in its square
    if (p.x >= hx - 1e-9 && p.x <= hx + r + 1e-9 && p.y >= hy - 1e-9 && p.y <= hy + r + 1e-9) {
      if (!Near(std::hypot(p.x - (hx + r), p.y - (hy + r)), r, 1e-9)) ++offCircle;
    }
  }
  d8test::Check(offCircle == 0, "curb return: the vertices of the north-east return lie on the circle of radius 6 around (11, 13), tangent to both kerb lines");
  bool tangentPoints = false;
  for (const P2& p : o) tangentPoints = tangentPoints || (Near(p.x, hx + r, 1e-9) && Near(p.y, hy, 1e-9));
  d8test::Check(tangentPoints, "curb return: the return starts on the E-W kerb line y = 7 at x = 11");
  d8test::Check(Near(PolyArea(IntersectionOutline(w1, w2, L, M, 0, 8)), 2060, 1e-9), "curb return: radius 0 gives the plain cross, 2060 m2");
  d8test::Check(!Near(PolyArea(o), 2060.0, 20.0), "curb return decoy: the returns add about 31 m2 to the cross");
  d8test::Check(Near(PolyArea(IntersectionOutline(w1, w2, L, M, 3, 64)) - 2060.0, 4 * 9 * (1 - kPi / 4), 0.01), "curb return: a 3 m radius adds a quarter of the 6 m radius area (7.73 m2 over all four)");
}

// ---- STREET-0002: intersections from approach streets, curb returns and conflict areas -------------------------------------
// Approach legs (bearing counter-clockwise from east, kerb-to-kerb width, length from the node centre), any number, any angle, given in any
// order. Corner between neighbouring legs with the free wedge angle theta: the fillet of radius r is tangent to both kerb lines at the
// distance r / tan(theta / 2) from their meeting point, its centre on the bisector at r / sin(theta / 2). It adds the kite r t minus the
// sector (r^2 / 2)(pi - theta) = r^2 / tan(theta / 2) - (r^2 / 2)(pi - theta) to the road; a chord polygon of n chords adds n circular
// segments (r^2 / 2)(phi - sin phi), phi = (pi - theta) / n, on top (the polygon road area is the exact one plus that).
//  * four legs 90 degrees apart, widths 14 / 10, lengths 50 / 40, r = 6 (given in the order N, E, W, S): strips 2 x 50 x 14 + 2 x 40 x 10
//    - 14 x 10 = 2060; four returns of 90 degrees: 4 x 7.72567 = 30.90266; chords (n = 64): 0.011354; total 2090.9140;
//  * a T: west and east legs 10 wide, north leg 8 wide, lengths 40, 40, 30, r = 6: 800 + 8 (30 - 5) = 1000, two returns 15.45133, chords
//    0.005677: 1015.4570; the straight side between west and east has no vertex;
//  * a skewed crossing, legs at 0, 60, 180, 240 degrees, widths 12 / 8 / 12 / 8, length 50, r = 5: two strips overlap in a parallelogram
//    of area 12 x 8 / sin 60 = 110.8513: 2 x 100 x 12 ... union 2000 - 110.8513 = 1889.1487; returns: two of 60 degrees (25 (cot 30 -
//    2 pi / 6) = 17.1213 each) and two of 120 degrees (25 (cot 60 - pi / 6) = 1.34378 each): 36.9302; chords 0.010513: 1926.0895.
// Tangency at every return: the tangent points are on the kerb lines (offset w/2 from the leg axis), the radii to them are perpendicular to
// the kerb direction and of length r, the tangent lengths from the corner are equal and r / tan(theta / 2), and every arc vertex of the
// outline is at distance r from the centre.
// Conflict areas: the overlaps of the half-carriageways (centre to leg end) of two legs. Perpendicular legs of widths a and b overlap in a
// b / 4: the cross has four such pairs of 35 m2 = 140 = 14 x 10 and no overlap between opposite legs; in the skewed crossing the four
// pairs partition the parallelogram: their sum is 110.8513, the two acute pairs equal each other and the two obtuse pairs equal each
// other, each checked against a 1 cm grid count made here (within 0.3 %).
bool InHalfStrip(double x, double y, double bearingDeg, double w, double len) {
  const double a = bearingDeg * kPi / 180, ux = std::cos(a), uy = std::sin(a);
  const double along = x * ux + y * uy, across = -x * uy + y * ux;
  return along >= 0 && along <= len && std::fabs(across) <= w / 2;
}
double ChordExtra(double r, double thetaRad, int n) {
  const double phi = (kPi - thetaRad) / n;
  return n * (r * r / 2) * (phi - std::sin(phi));
}
double FilletArea(double r, double thetaRad) { return r * r / std::tan(thetaRad / 2) - (r * r / 2) * (kPi - thetaRad); }
D8_EXTRA_TEST(Rw5FormaIntersectionApproachLegs) {
  const int n = 64;
  // the cross, legs in the order N, E, W, S
  const std::vector<ApproachLeg> cross = {{90, 10, 40}, {0, 14, 50}, {180, 14, 50}, {270, 10, 40}};
  const IntersectionGeometry gc = BuildIntersection(cross, 6, n);
  const double crossExpect = 2060.0 + 4 * FilletArea(6, kPi / 2) + 4 * ChordExtra(6, kPi / 2, n);
  d8test::Check(gc.ok && Near(2060.0 + 4 * FilletArea(6, kPi / 2), 2090.9027, 1e-4) && Near(crossExpect, 2090.9140, 1e-4) && Near(PolyArea(gc.outline), crossExpect, 1e-8) && PolygonArea2(gc.outline) > 0,
                "intersection: four legs in the order N, E, W, S, widths 14 / 10, r = 6: area 2090.9140 m2 (strips 2060 + returns 30.9027 + chords 0.0114), counter-clockwise");
  d8test::Check(gc.order.size() == 4 && gc.order[0] == 1 && gc.order[1] == 0 && gc.order[2] == 2 && gc.order[3] == 3 && gc.returns.size() == 4 && gc.outline.size() == 4 * (2 + n + 1),
                "intersection: the legs are ordered by bearing (E, N, W, S) and the outline has 2 end points and 65 arc points per corner");
  // equal to the symmetric builder of the earlier test
  d8test::Check(Near(PolyArea(gc.outline), PolyArea(IntersectionOutline(14, 10, 50, 40, 6, n)), 1e-8), "intersection: the general builder gives the symmetric cross of the earlier closed form");
  // tangency at every return of the cross, the T and the skewed crossing
  const std::vector<ApproachLeg> tj = {{180, 10, 40}, {0, 10, 40}, {90, 8, 30}};
  const std::vector<ApproachLeg> skew = {{0, 12, 50}, {60, 8, 50}, {180, 12, 50}, {240, 8, 50}};
  int tangencyBad = 0, arcBad = 0;
  for (const auto* legsPtr : {&cross, &tj, &skew}) {
    const auto& legs = *legsPtr;
    const IntersectionGeometry g = BuildIntersection(legs, legs.size() == 4 && legs[0].bearingDeg == 90 ? 6.0 : (legs.size() == 3 ? 6.0 : 5.0), n);
    for (const CurbReturn& r : g.returns) {
      if (!r.rounded) continue;
      const ApproachLeg &A = legs[r.legA], &B = legs[r.legB];
      const double ra = A.bearingDeg * kPi / 180, rb = B.bearingDeg * kPi / 180;
      const double uax = std::cos(ra), uay = std::sin(ra), ubx = std::cos(rb), uby = std::sin(rb);
      const double th = r.wedgeDeg * kPi / 180;
      // tangent point A on the left kerb of A: offset +w/2 from its axis; B on the right kerb of B: offset -w/2
      if (!Near(-r.tangentA.x * uay + r.tangentA.y * uax, A.width / 2, 1e-9) || !Near(-r.tangentB.x * uby + r.tangentB.y * ubx, -B.width / 2, 1e-9)) ++tangencyBad;
      // radius to the tangent points: length r and perpendicular to the kerb direction
      const double dax = r.tangentA.x - r.centre.x, day = r.tangentA.y - r.centre.y, dbx = r.tangentB.x - r.centre.x, dby = r.tangentB.y - r.centre.y;
      if (!Near(std::hypot(dax, day), r.radius, 1e-9) || !Near(std::hypot(dbx, dby), r.radius, 1e-9) || !Near(dax * uax + day * uay, 0, 1e-9) || !Near(dbx * ubx + dby * uby, 0, 1e-9)) ++tangencyBad;
      // tangent lengths from the kerb corner
      const double ta = std::hypot(r.tangentA.x - r.corner.x, r.tangentA.y - r.corner.y), tb = std::hypot(r.tangentB.x - r.corner.x, r.tangentB.y - r.corner.y);
      if (!Near(ta, r.radius / std::tan(th / 2), 1e-9) || !Near(tb, ta, 1e-9)) ++tangencyBad;
    }
    // the arc starts and ends exactly at the tangent points: both are vertices of the outline
    for (const CurbReturn& r : g.returns) {
      if (!r.rounded) continue;
      bool hasA = false, hasB = false;
      for (const P2& v : g.outline) {
        hasA = hasA || std::hypot(v.x - r.tangentA.x, v.y - r.tangentA.y) < 1e-9;
        hasB = hasB || std::hypot(v.x - r.tangentB.x, v.y - r.tangentB.y) < 1e-9;
      }
      if (!hasA || !hasB) ++tangencyBad;
    }
    // every outline vertex is on a kerb line, an end line, or on the circle of some return
    for (const P2& v : g.outline) {
      bool onCircle = false;
      for (const CurbReturn& r : g.returns)
        if (r.rounded && Near(std::hypot(v.x - r.centre.x, v.y - r.centre.y), r.radius, 1e-9)) onCircle = true;
      bool onKerbOrEnd = false;
      for (const ApproachLeg& l : legs) {
        const double a = l.bearingDeg * kPi / 180, across = -v.x * std::sin(a) + v.y * std::cos(a), along = v.x * std::cos(a) + v.y * std::sin(a);
        if (Near(std::fabs(across), l.width / 2, 1e-9) || Near(along, l.length, 1e-9)) onKerbOrEnd = true;
      }
      if (!onCircle && !onKerbOrEnd) ++arcBad;
    }
  }
  d8test::Check(tangencyBad == 0, "intersection: at all returns of the cross, the T and the skewed crossing the tangent points are on the kerb lines, the radii are r and perpendicular to the kerbs, the tangent lengths are r / tan(theta/2)");
  d8test::Check(arcBad == 0, "intersection: every outline vertex is on a kerb line, on a leg end or on the circle of a return");
  // the T
  const IntersectionGeometry gt = BuildIntersection(tj, 6, n);
  const double tExpect = 1000.0 + 2 * FilletArea(6, kPi / 2) + 2 * ChordExtra(6, kPi / 2, n);
  int sharp = 0;
  for (const CurbReturn& r : gt.returns) sharp += r.rounded ? 0 : 1;
  d8test::Check(gt.ok && Near(tExpect, 1015.4570, 1e-4) && Near(PolyArea(gt.outline), tExpect, 1e-8) && gt.returns.size() == 3 && sharp == 1 && gt.outline.size() == 3 * 2 + 2 * (n + 1),
                "intersection: a T of 10 m through street and an 8 m side street: 1000 + 15.4513 + 0.0057 = 1015.4570 m2, the straight side has no return and no vertex");
  // the skewed crossing
  const IntersectionGeometry gs = BuildIntersection(skew, 5, n);
  const double para = 12.0 * 8.0 / std::sin(kPi / 3);
  const double sExpect = 2000.0 - para + 2 * FilletArea(5, kPi / 3) + 2 * FilletArea(5, 2 * kPi / 3) + 2 * ChordExtra(5, kPi / 3, n) + 2 * ChordExtra(5, 2 * kPi / 3, n);
  d8test::Check(gs.ok && Near(para, 110.8513, 1e-4) && Near(sExpect, 1926.0895, 1e-4) && Near(PolyArea(gs.outline), sExpect, 1e-8) && Near(gs.returns[0].wedgeDeg, 60, 1e-9) && Near(gs.returns[1].wedgeDeg, 120, 1e-9),
                "intersection: a crossing at 60 degrees, widths 12 and 8, r = 5: 1889.1487 + 36.9302 + 0.0105 = 1926.0895 m2, wedges 60 and 120 degrees");
  // sharp corners: radius 0
  d8test::Check(Near(PolyArea(BuildIntersection(cross, 0, n).outline), 2060.0, 1e-9) && Near(PolyArea(BuildIntersection(tj, 0, n).outline), 1000.0, 1e-9) && Near(PolyArea(BuildIntersection(skew, 0, n).outline), 2000.0 - para, 1e-9),
                "intersection: radius 0 gives the plain unions of the strips: 2060, 1000 and 1889.1487 m2");
  // a bend of two legs 90 degrees apart: widths 10 (east) and 8 (north), lengths 30. The outer (reflex) corner is a sharp kerb corner at
  // (-4, -5), so the road is two rectangles: [-4, 30] x [-5, 5] = 340 and [-4, 4] x [5, 30] = 200, 540 m2, and the inner corner gets one
  // return of r = 6 (90 degrees): 7.72567 + 0.00284 chords = 547.7285
  const IntersectionGeometry gl = BuildIntersection({{0, 10, 30}, {90, 8, 30}}, 6, n);
  const double lExpect = 34.0 * 10 + 8.0 * 25 + FilletArea(6, kPi / 2) + ChordExtra(6, kPi / 2, n);
  d8test::Check(gl.ok && Near(PolyArea(gl.outline), lExpect, 1e-8) && Near(lExpect, 547.7285, 1e-4) && gl.returns.size() == 2 && gl.returns[0].rounded != gl.returns[1].rounded,
                "intersection: a bend of two legs: two rectangles 340 + 200 plus one return = 547.7285 m2, the outer corner is sharp");
  // errors
  d8test::Check(!BuildIntersection({{0, 10, 8}, {90, 10, 8}}, 6, n).ok && !BuildIntersection({{0, 10, 40}}, 6, n).ok && !BuildIntersection({{0, 10, 40}, {360, 10, 40}}, 6, n).ok && !BuildIntersection({{0, 10, 40}, {180, 12, 40}}, 6, n).ok &&
                    !BuildIntersection({{0, 0, 40}, {90, 10, 40}}, 6, n).ok && BuildIntersection({{0, 10, 8}, {90, 10, 8}}, 6, n).error.find("radius too large") != std::string::npos,
                "intersection: a return too large for short legs, one leg, two legs at the same bearing (0 and 360), opposite legs of different width and a zero width are refused with a reason");
  const ApproachLeg lt = LegToward({100, 200}, {100, 250}, 10), l2 = LegToward({0, 0}, {-3, -4}, 6);
  d8test::Check(Near(lt.bearingDeg, 90, 1e-12) && Near(lt.length, 50, 1e-12) && lt.width == 10 && Near(l2.length, 5, 1e-12) && Near(l2.bearingDeg, 180 + std::atan2(4.0, 3.0) * 180 / kPi, 1e-9), "intersection: a leg from a node towards a centreline point has the bearing and length of that segment");
  d8test::Check(Near(PolyArea(BuildIntersection({{-270, 10, 40}, {360, 14, 50}, {-180, 14, 50}, {-90, 10, 40}}, 6, n).outline), PolyArea(gc.outline), 1e-8),
                "intersection: bearings are taken modulo 360 (-270, 360, -180, -90 are N, E, W, S)");
  // conflict areas
  d8test::Check(gc.conflicts.size() == 4, "conflicts: four pairs of perpendicular legs overlap in the cross, opposite legs do not");
  double confSum = 0;
  bool allThirtyFive = true;
  for (const ConflictArea& c : gc.conflicts) {
    confSum += c.area;
    allThirtyFive = allThirtyFive && Near(c.area, 14.0 * 10.0 / 4.0, 1e-9);
  }
  d8test::Check(allThirtyFive && Near(confSum, 140.0, 1e-9), "conflicts: each perpendicular pair overlaps in 14 x 10 / 4 = 35 m2, together 140 = 14 x 10");
  double skewSum = 0;
  for (const ConflictArea& c : gs.conflicts) skewSum += c.area;
  d8test::Check(gs.conflicts.size() == 4 && Near(skewSum, para, 1e-9), "conflicts: the four half-carriageway pairs of the skewed crossing add to the parallelogram 12 x 8 / sin 60 = 110.8513 m2");
  // each pair against a 1 cm grid count written here
  auto gridArea = [&](const ApproachLeg& a, const ApproachLeg& b) {
    long cells = 0;
    const double h = 0.01;
    for (double y = -12 + h / 2; y < 12; y += h)
      for (double x = -12 + h / 2; x < 12; x += h)
        if (InHalfStrip(x, y, a.bearingDeg, a.width, a.length) && InHalfStrip(x, y, b.bearingDeg, b.width, b.length)) ++cells;
    return cells * h * h;
  };
  double worstRel = 0;
  for (const ConflictArea& c : gs.conflicts) {
    const double g = gridArea(skew[c.legA], skew[c.legB]);
    worstRel = std::max(worstRel, std::fabs(g - c.area) / c.area);
  }
  d8test::Check(worstRel < 0.003, "conflicts: every pair's overlap agrees with an independent 1 cm grid count within 0.3 %");
  bool acuteEqual = false, obtuseEqual = false;
  {
    double a02 = 0, a31 = 0, a03 = 0, a12 = 0;
    for (const ConflictArea& c : gs.conflicts) {
      if (c.legA == 0 && c.legB == 1) a02 = c.area;
      if (c.legA == 2 && c.legB == 3) a31 = c.area;
      if (c.legA == 0 && c.legB == 3) a03 = c.area;
      if (c.legA == 1 && c.legB == 2) a12 = c.area;
    }
    acuteEqual = a02 > a03 && Near(a02, a31, 1e-9);
    obtuseEqual = Near(a03, a12, 1e-9) && a03 > 0;
  }
  d8test::Check(acuteEqual && obtuseEqual, "conflicts: the two acute pairs are equal and larger than the two obtuse pairs, which are equal (point symmetry of the crossing)");
  d8test::Check(!Near(PolyArea(gc.outline), 2060.0, 20.0) && !Near(FilletArea(6, kPi / 2), 6 * 6 * (1 - kPi / 2), 1.0), "intersection decoy: the returns add 31 m2 to the cross, and the fillet area is not the (wrong) formula r^2 (1 - pi/2)");
}

}  // namespace
