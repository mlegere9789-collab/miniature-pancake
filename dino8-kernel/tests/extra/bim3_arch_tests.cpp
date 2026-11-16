// Architecture calculations of include/dino8/kernel/bim3_calc.h: sloped floors / roofs, layered floors, railings, supports,
// spiral and winder treads, room regions and volumes, opening schedules, door swing. One test per Register row. Every expected
// value is a closed form worked out here (derivation in the comment of each test; decimal literals computed outside the code under
// test) or comes from a route that shares no code with the function (3D cross products, brute-force trial loops, Euler's formula
// on hand-counted graphs, midpoint integration). Selection, undo and UI clauses of the rows are not tested.
#include <algorithm>
#include <cmath>
#include <vector>

#include "dino8/kernel/bim3_calc.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::bim3;

bool Near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

// ---------------------------------------------------------------------------------------------------------------------------
// D8-REVIT-ARCH-0023: "Slope a floor either by drawing a slope arrow in the boundary sketch, by giving parallel boundary lines an
// offset from base, or by marking a single boundary line as slope-defining with a slope value."
// Closed forms: arrow (0,0) z=0.10 to (4,0) z=0.30 -> slope 0.20/4 = 0.05, z(2,5) = 0.10 + 0.05*2 = 0.20. Arrow (0,0)->(3,4) (length 5)
// from z=0 to z=0.5 -> slope 0.1 along (0.6, 0.8); z(6,8) = 0.1*10 = 1.0; z(4,-3) = 0.1*(2.4-2.4) = 0.
// Parallel lines y=0 (z=0) and y=6 (z=0.3): slope 0.3/6 = 0.05, z(7,3) = 0.15. Slope-defining line y=0, z=0.2, slope 0.02, left
// side: z(5,10) = 0.2 + 0.02*10 = 0.4; right side: z(5,-10) = 0.4 and z(5,10) = 0.2 - 0.2 = 0.
D8_EXTRA_TEST(Rw3FloorSlopeThreeWays) {
  SlopePlane a;
  d8test::Check(SlopeFromArrow({0, 0}, {4, 0}, 0.10, 0.30, a), "floor slope: arrow accepted");
  d8test::Check(Near(a.slope, 0.05) && Near(a.ZAt(2, 5), 0.20) && Near(a.ZAt(4, -9), 0.30), "floor slope: arrow 0.10 -> 0.30 over 4 m gives 5 %, z(2,5)=0.20");
  SlopePlane b;
  d8test::Check(SlopeFromArrow({0, 0}, {3, 4}, 0.0, 0.5, b), "floor slope: skew arrow accepted");
  d8test::Check(Near(b.slope, 0.1) && Near(b.ZAt(6, 8), 1.0) && Near(b.ZAt(4, -3), 0.0, 1e-12), "floor slope: skew arrow rises 0.1 per metre along (0.6,0.8), flat across it");
  d8test::Check(!SlopeFromArrow({1, 1}, {1, 1}, 0, 1, a), "floor slope: a zero-length arrow is refused");
  SlopePlane p;
  d8test::Check(SlopeFromParallelLines({0, 0}, {10, 0}, 0.0, {0, 6}, {10, 6}, 0.3, p), "floor slope: parallel lines accepted");
  d8test::Check(Near(p.slope, 0.05) && Near(p.ZAt(7, 3), 0.15) && Near(p.ZAt(7, 6), 0.3), "floor slope: lines 6 m apart with 0.3 offset give 5 %");
  SlopePlane q;
  d8test::Check(SlopeFromParallelLines({0, 0}, {10, 0}, 0.0, {0, -6}, {10, -6}, 0.3, q) && Near(q.ZAt(0, -3), 0.15) && Near(q.ZAt(5, 4), -0.2),
                "floor slope: the second line on the other side makes the plane rise toward -y");
  d8test::Check(!SlopeFromParallelLines({0, 0}, {10, 0}, 0.0, {0, 6}, {10, 7}, 0.3, p), "floor slope: non-parallel lines are refused");
  d8test::Check(!SlopeFromParallelLines({0, 0}, {10, 0}, 0.0, {3, 0}, {8, 0}, 0.3, p), "floor slope: coincident lines are refused");
  SlopePlane s1, s2;
  d8test::Check(SlopeFromDefiningLine({0, 0}, {10, 0}, 0.2, 0.02, +1, s1) && SlopeFromDefiningLine({0, 0}, {10, 0}, 0.2, 0.02, -1, s2), "floor slope: defining line accepted");
  d8test::Check(Near(s1.ZAt(5, 10), 0.4) && Near(s1.ZAt(5, -10), 0.0) && Near(s2.ZAt(5, -10), 0.4) && Near(s2.ZAt(5, 10), 0.0), "floor slope: defining line rises 2 % to the chosen side");
  // the three descriptions of one 5 % plane agree everywhere on a grid
  SlopePlane c;
  SlopeFromDefiningLine({0, 0}, {10, 0}, 0.0, 0.05, +1, c);
  double worst = 0;
  for (double x = -3; x <= 9; x += 1.5)
    for (double y = -2; y <= 8; y += 1.25) worst = std::max(worst, std::fabs(c.ZAt(x, y) - p.ZAt(x, y)));
  d8test::Check(worst < 1e-12, "floor slope: parallel-lines and defining-line planes of the same 5 % slope coincide on a grid");
  d8test::Check(Near(p.Percent(), 5.0) && Near(p.AngleDeg(), 2.8624052261117474, 1e-12), "floor slope: 5 % is atan(0.05) = 2.86240523 degrees");
}

// D8-REVIT-ARCH-0025: "Let exactly one layer of a floor or roof type be flagged as variable thickness so that, when the element's
// shape is edited, its top surface slopes while the underside stays horizontal and layers above follow the sloped top."
// Stack (bottom up): structure 0.20, insulation 0.05 (variable), screed 0.04, finish 0.01; base z = 0, nominal total 0.30. With the top
// 0.03 higher: layer faces 0 | 0.20 | 0.28 | 0.32 | 0.33, total 0.33. At -0.05 the variable layer vanishes; at -0.06 it is refused.
// Volume of the variable layer over a 4 x 3 plan whose top shift is 0.01 * x: mean shift = 0.01 * 2 (centroid), V = 12 * 0.07 = 0.84,
// cross-checked by a 400 x 300 midpoint sum.
D8_EXTRA_TEST(Rw3VariableThicknessLayer) {
  const std::vector<Layer> st = {{"structure", 0.20, false}, {"insulation", 0.05, true}, {"screed", 0.04, false}, {"finish", 0.01, false}};
  LayerStack r = StackAtPoint(st, 0.0, 0.03);
  d8test::Check(r.valid && r.bottom.size() == 4, "variable layer: stack built");
  d8test::Check(Near(r.bottom[0], 0) && Near(r.top[0], 0.20) && Near(r.bottom[1], 0.20) && Near(r.top[1], 0.28) && Near(r.bottom[2], 0.28) && Near(r.top[2], 0.32) && Near(r.top[3], 0.33) && Near(r.total, 0.33),
                "variable layer: underside stays at 0, only the flagged layer grows, layers above follow the raised top");
  r = StackAtPoint(st, 1.5, 0.0);
  d8test::Check(r.valid && Near(r.total, 0.30) && Near(r.bottom[0], 1.5), "variable layer: no shift gives the nominal 0.30 total on a raised base");
  r = StackAtPoint(st, 0.0, -0.05);
  d8test::Check(r.valid && Near(r.top[1], r.bottom[1]) && Near(r.total, 0.25), "variable layer: shifting down by its full thickness leaves a zero-thickness layer");
  r = StackAtPoint(st, 0.0, -0.06);
  d8test::Check(!r.valid && !r.error.empty(), "variable layer: a shift beyond the layer thickness is refused");
  std::vector<Layer> two = st;
  two[0].variable = true;
  d8test::Check(!StackAtPoint(two, 0, 0).valid, "variable layer: two flagged layers are refused");
  std::vector<Layer> none = st;
  none[1].variable = false;
  d8test::Check(!StackAtPoint(none, 0, 0).valid, "variable layer: no flagged layer is refused");
  const std::vector<P2> plan = {{0, 0}, {4, 0}, {4, 3}, {0, 3}};
  SlopePlane shift{0, 0, 0, 1, 0, 0.01};
  const double v = VariableLayerVolume(0.05, plan, shift);
  double sum = 0;
  const int nx = 400, ny = 300;
  for (int i = 0; i < nx; ++i)
    for (int j = 0; j < ny; ++j) sum += (4.0 / nx) * (3.0 / ny) * (0.05 + 0.01 * (4.0 * (i + 0.5) / nx));
  d8test::Check(Near(v, 0.84, 1e-12) && Near(v, sum, 1e-9), "variable layer: volume 12 x (0.05 + 0.02) = 0.84, equal to the midpoint sum");
}

// D8-REVIT-ARCH-0107: "Use slope arrows within the footprint to define one-way pitch."
// Arrow from (0,0) at 3.0 to (0,6) at 6.0: slope 0.5, 26.565051177 degrees. The roof over an 8 x 6 footprint is a plane; its area is the
// magnitude of the cross product of two 3D edge vectors (8,0,0) x (0,6,3) = 8 * sqrt(45) = 53.66563146 (no slope formula involved).
D8_EXTRA_TEST(Rw3RoofSlopeArrowOneWay) {
  SlopePlane r;
  d8test::Check(SlopeFromArrow({0, 0}, {0, 6}, 3.0, 6.0, r), "roof slope arrow: accepted");
  d8test::Check(Near(r.slope, 0.5) && Near(r.AngleDeg(), 26.56505117707799, 1e-9) && Near(r.ZAt(5, 2), 4.0) && Near(r.ZAt(-9, 6), 6.0), "roof slope arrow: pitch 0.5 = 26.565 degrees, heights follow y only");
  const double e1[3] = {8, 0, r.ZAt(8, 0) - r.ZAt(0, 0)}, e2[3] = {0, 6, r.ZAt(0, 6) - r.ZAt(0, 0)};
  const double cx = e1[1] * e2[2] - e1[2] * e2[1], cy = e1[2] * e2[0] - e1[0] * e2[2], cz = e1[0] * e2[1] - e1[1] * e2[0];
  d8test::Check(Near(std::sqrt(cx * cx + cy * cy + cz * cz), 53.665631459994955, 1e-9), "roof slope arrow: one-way roof area equals the 3D cross product 8 sqrt(45)");
  SlopePlane flat;
  d8test::Check(SlopeFromArrow({0, 0}, {5, 0}, 2.0, 2.0, flat) && Near(flat.slope, 0) && Near(flat.AngleDeg(), 0), "roof slope arrow: equal heights give a flat roof");
}

// D8-REVIT-ARCH-0028: "Create a roof from a sketched footprint with per-edge slope definitions and overhangs."
// Footprint 10 (x) by 6 (y), slope 0.5, overhang 0.5. Hip: ridge height 0.5 * 6/2 = 1.5, ridge length 10 - 6 = 4, eave drop 0.25.
// Surface area by facets: plan with overhang 11 x 7; the two long facets are trapezoids (11 + 4)/2 wide and 3.5 deep in plan, the two
// ends triangles of base 7 and depth 3.5; each facet's true area is plan area * sqrt(1.25) = 3.5*sqrt(1.25)=3.913118960624632 per metre of plan depth.
// Volume by decomposition: a pyramid on 6 x 6 (36 * 1.5 / 3 = 18) plus a prism (0.5 * 6 * 1.5 * 4 = 18) = 36. Gable (south and north
// edges sloped): volume 0.5 * 6 * 1.5 * 10 = 45, two facets of 11 x 3.913118960624632 -> 86.0886171337419.
D8_EXTRA_TEST(Rw3RoofFootprintEdgeSlopes) {
  RoofRect r;
  r.width = 6;
  r.length = 10;
  r.slope = 0.5;
  r.overhang = 0.5;
  RoofResult hip = AnalyseRoof(r);
  const double slope_depth = 3.5 * std::sqrt(1.25);
  const double facets = 2 * ((11.0 + 4.0) / 2.0 * slope_depth) + 2 * (0.5 * 7.0 * slope_depth);
  d8test::Check(hip.valid && hip.kind == "hip" && hip.facets == 4, "roof footprint: all four edges sloped is a hip roof of 4 facets");
  d8test::Check(Near(hip.ridge_height, 1.5) && Near(hip.ridge_length, 4.0) && Near(hip.eave_drop, 0.25), "roof footprint: hip ridge 1.5 high, 4 long, eave drop 0.25");
  d8test::Check(Near(hip.surface_area, facets, 1e-9) && Near(hip.surface_area, 86.0886171337419, 1e-9), "roof footprint: hip area equals the sum of its four facets, 86.0886");
  d8test::Check(Near(hip.volume, 18.0 + 18.0, 1e-9), "roof footprint: hip volume equals pyramid 18 plus prism 18");
  r.edge_sloped[1] = r.edge_sloped[3] = false;
  RoofResult g = AnalyseRoof(r);
  d8test::Check(g.valid && g.kind == "gable-x" && g.facets == 2 && Near(g.ridge_height, 1.5) && Near(g.ridge_length, 11.0), "roof footprint: south/north sloped is a gable with the ridge along x, 11 long with the rake overhang");
  d8test::Check(Near(g.volume, 45.0) && Near(g.surface_area, 2 * 11.0 * slope_depth, 1e-9), "roof footprint: gable volume 45, area two 11 x 3.9131 facets");
  r.edge_sloped[0] = r.edge_sloped[2] = false;
  r.edge_sloped[1] = true;
  d8test::Check(!AnalyseRoof(r).valid, "roof footprint: a single sloped edge is not a supported footprint");
  RoofRect bad;
  d8test::Check(!AnalyseRoof(bad).valid, "roof footprint: zero size is refused");
}

// D8-REVIT-ARCH-0034: "Mark individual boundary edges as slope-defining to create hip/gable mixtures."
// Unflagging the east edge of the 10 x 6 footprint (no overhang, slope 0.5): hip at the west end, gable at the east. Ridge 1.5 high,
// 10 - 3 = 7 long. Volume: gable prism 0.5 * 6 * 1.5 * 10 = 45 minus the pyramid cut at the hipped end: base the end triangle
// (0.5 * 6 * 1.5 = 4.5), height 3, one third -> 4.5 ; 45 - 4.5 = 40.5 (and the full hip of 36 = 45 - 2*4.5 confirms the pyramid).
D8_EXTRA_TEST(Rw3RoofHipGableMixture) {
  RoofRect r;
  r.width = 6;
  r.length = 10;
  r.slope = 0.5;
  r.edge_sloped[1] = false;
  RoofResult m = AnalyseRoof(r);
  d8test::Check(m.valid && m.kind == "half-hip" && m.facets == 3, "hip/gable mixture: three sloped edges give a hip at one end and a gable at the other");
  d8test::Check(Near(m.ridge_height, 1.5) && Near(m.ridge_length, 7.0), "hip/gable mixture: ridge 1.5 high and 7 long");
  d8test::Check(Near(m.volume, 45.0 - 4.5), "hip/gable mixture: volume 40.5 = gable prism minus one pyramid");
  RoofRect hip = r;
  hip.edge_sloped[1] = true;
  RoofRect gable = r;
  gable.edge_sloped[1] = gable.edge_sloped[3] = false;
  const double vh = AnalyseRoof(hip).volume, vg = AnalyseRoof(gable).volume, vm = m.volume;
  d8test::Check(Near(vh, 36.0) && Near(vg, 45.0), "hip/gable mixture: reference hip 36 and gable 45");
  d8test::Check(Near(vg - vm, vm - vh) && Near(vg - vm, 4.5), "hip/gable mixture: each hipped end removes the same 4.5 (gable 45, half-hip 40.5, hip 36)");
  RoofRect east = r;
  east.edge_sloped[1] = true;
  east.edge_sloped[0] = false;
  RoofResult e = AnalyseRoof(east);
  d8test::Check(e.valid && e.kind == "half-hip" && Near(e.ridge_height, 2.5) && Near(e.ridge_length, 1.0) && Near(e.volume, 75.0 - 100.0 * 2.5 / 12.0, 1e-9), "hip/gable mixture: unflagging a long edge turns the ridge to run along y (span 10, ridge 2.5 high and 1 long, volume 75 - 250/12)");
}

// D8-REVIT-ARCH-0048: "Create a railing along a sketched path with type-defined posts, rails and balusters."
// Straight 3.0 m path, posts at most 1.2 m apart -> ceil(2.5) = 3 equal bays of 1.0 m: posts at 0,1,2,3. Balusters 0.02 wide with clear
// gap <= 0.1: the smallest n with (1.0 - 0.02 n)/(n+1) <= 0.1 found by trial (n=7 gives 0.1075, n=8 gives 0.09333...), 8 per bay, 24 in
// all; first centre at gap + 0.01 = 0.10333..., centre pitch gap + 0.02 = 0.11333.... L-path (0,0)-(2,0)-(2,1.5): bays 1,1 then 0.75,0.75.
D8_EXTRA_TEST(Rw3RailingLayoutByPath) {
  RailingPlan p = PlanRailing({{0, 0}, {3, 0}}, 1.2, 0.02, 0.1);
  d8test::Check(Near(p.length, 3.0) && p.posts.size() == 4 && Near(p.posts[1], 1.0) && Near(p.posts[3], 3.0) && p.bays == 3, "railing: 3 m path with 1.2 m maximum spacing has 4 posts in 3 equal bays");
  int n_trial = 0;
  while ((1.0 - 0.02 * n_trial) / (n_trial + 1) > 0.1) ++n_trial;
  d8test::Check(n_trial == 8 && BalustersPerBay(1.0, 0.02, 0.1) == 8, "railing: the formula and a trial loop both give 8 balusters per bay");
  d8test::Check(p.balusters.size() == 24, "railing: 24 balusters on three bays");
  const double gap = (1.0 - 0.16) / 9.0;
  d8test::Check(Near(p.balusters[0], gap + 0.01, 1e-12) && Near(p.balusters[1] - p.balusters[0], gap + 0.02, 1e-12) && Near(p.balusters[8], 1.0 + gap + 0.01, 1e-12), "railing: baluster centres are evenly pitched inside each bay");
  double worst_gap = 0;
  for (size_t i = 1; i < 8; ++i) worst_gap = std::max(worst_gap, p.balusters[i] - p.balusters[i - 1] - 0.02);
  worst_gap = std::max(worst_gap, p.balusters[0] - 0.01);
  d8test::Check(worst_gap <= 0.1 && worst_gap > 0.09, "railing: the clear gap of 0.0933 stays under the 0.1 limit");
  RailingPlan l = PlanRailing({{0, 0}, {2, 0}, {2, 1.5}}, 1.2, 0.02, 0.1);
  d8test::Check(Near(l.length, 3.5) && l.posts.size() == 5 && Near(l.posts[2], 2.0) && Near(l.posts[3], 2.75) && Near(l.posts[4], 3.5), "railing: a corner post is shared by both segments (5 posts on the L path)");
  d8test::Check(l.bays == 4 && l.balusters.size() == (size_t)(8 + 8 + 2 * BalustersPerBay(0.75, 0.02, 0.1)), "railing: bay counts add up across the corner");
  d8test::Check(BalustersPerBay(0.08, 0.02, 0.1) == 0 && PostStations(0, 1).empty(), "railing: a bay narrower than the clear limit needs no baluster; empty path has no posts");
}

// D8-REVIT-ARCH-0049: "Place railing on a stair or ramp host."
// Two flights of 9 risers (0.18) and treads 0.28 with a 1.0 m landing: each flight runs (9-1)*0.28 = 2.24 horizontally and rises
// 9*0.18 = 1.62, slope length sqrt(2.24^2 + 1.62^2) = 2.764416755845616. Profile (0,0) (2.24,1.62) (3.24,1.62) (5.48,3.24); path length
// 2*2.764416755845616 + 1.0 = 6.528833511691232. A 0.9 m rail follows the host: at s = 1.12 the nosing is at 0.81, rail 1.71; on the
// landing the rail is at 2.52; the "level" option holds the start height 0.9.
D8_EXTRA_TEST(Rw3RailingOnStairHost) {
  const std::vector<StairFlight> fl = {{9, 0.18, 0.28}, {9, 0.18, 0.28}};
  const std::vector<P2> prof = StairNosingProfile(fl, 1.0);
  d8test::Check(prof.size() == 4 && Near(prof[1].x, 2.24) && Near(prof[1].y, 1.62) && Near(prof[2].x, 3.24) && Near(prof[3].x, 5.48) && Near(prof[3].y, 3.24), "stair railing: nosing profile of two flights and a landing");
  const RailingPlan p = PlanRailing(prof, 1.0, 0.02, 0.1);
  d8test::Check(Near(p.length, 6.528833511691232, 1e-9), "stair railing: path length is two slope lengths plus the landing");
  d8test::Check(Near(TopRailElevation(prof, 1.12, 0.9, true), 1.71) && Near(TopRailElevation(prof, 2.74, 0.9, true), 2.52) && Near(TopRailElevation(prof, 5.48, 0.9, true), 4.14), "stair railing: follow-host rail height is host height plus 0.9");
  d8test::Check(Near(TopRailElevation(prof, 1.12, 0.9, false), 0.9) && Near(TopRailElevation(prof, 5.0, 0.9, false), 0.9), "stair railing: the level option keeps the start height");
  d8test::Check(p.posts.size() >= 2 && Near(p.posts.front(), 0) && Near(p.posts.back(), p.length, 1e-9), "stair railing: posts at both ends of the path");
}

// D8-REVIT-ARCH-0151: "Define up to two handrails per railing type and place supports by layout rules, with individual supports
// unpinned to override position along the rail."
// Rail 2.0 m. Fixed distance 0.6: from the beginning 0,0.6,1.2,1.8; from the end 0.2,0.8,1.4,2.0; centred m=3 -> start (2-1.8)/2 = 0.1:
// 0.1,0.7,1.3,1.9. Maximum spacing 0.6: ceil(3.33)=4 bays -> 0,0.5,1,1.5,2. Fixed number 5 -> pitch 0.5; 1 -> middle. Unpinning index 2 to 1.1.
D8_EXTRA_TEST(Rw3HandrailSupportLayout) {
  auto same = [](const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (std::fabs(a[i] - b[i]) > 1e-9) return false;
    return true;
  };
  d8test::Check(same(SupportStations(2.0, SupportRule::FixedDistance, 0.6, Justify::Beginning), {0, 0.6, 1.2, 1.8}), "supports: fixed distance from the beginning");
  d8test::Check(same(SupportStations(2.0, SupportRule::FixedDistance, 0.6, Justify::End), {0.2, 0.8, 1.4, 2.0}), "supports: fixed distance from the end");
  d8test::Check(same(SupportStations(2.0, SupportRule::FixedDistance, 0.6, Justify::Center), {0.1, 0.7, 1.3, 1.9}), "supports: fixed distance centred");
  d8test::Check(same(SupportStations(2.0, SupportRule::MaxSpacing, 0.6, Justify::Beginning), {0, 0.5, 1.0, 1.5, 2.0}), "supports: maximum spacing makes equal bays");
  d8test::Check(same(SupportStations(2.0, SupportRule::FixedNumber, 5, Justify::Beginning), {0, 0.5, 1.0, 1.5, 2.0}) && same(SupportStations(2.0, SupportRule::FixedNumber, 1, Justify::Beginning), {1.0}), "supports: fixed number");
  d8test::Check(same(OverrideSupport({0, 0.5, 1.0, 1.5, 2.0}, 2, 1.1), {0, 0.5, 1.1, 1.5, 2.0}) && same(OverrideSupport({0, 1}, 7, 0.3), {0, 1}), "supports: an unpinned support moves, the others stay");
  d8test::Check(HandrailCountAllowed(0) && HandrailCountAllowed(2) && !HandrailCountAllowed(3), "supports: at most two handrails per railing type");
  d8test::Check(SupportStations(0, SupportRule::FixedDistance, 0.6, Justify::Beginning).empty(), "supports: a zero-length rail has none");
}

// D8-REVIT-ARCH-0096: "Create common stair runs as straight, full-step spiral, centre-ends spiral, L-shape winder or U-shape winder,
// or sketch a custom run." (the centre-ends spiral variant is not modelled here)
// Full-step spiral of 12 treads over 360 degrees, radii 0.1 / 1.0, walkline 0.55: each sector 30 degrees = pi/6, walkline depth
// 0.55*pi/6 = 0.2879793265790644, area 0.5*(pi/6)*(1-0.01) = 0.2591813939211579, all 12 = pi*0.99 = 3.1101767270538954 (annulus).
// L winder: 90 degrees in 3 treads = 30 each; U winder: 180 degrees in 6 treads. A straight run of 14 risers has 13 treads.
D8_EXTRA_TEST(Rw3SpiralAndWinderTreads) {
  const SectorTread t = SectorTreadGeometry(360.0, 12, 0.1, 1.0, 0.55);
  d8test::Check(Near(t.angle_rad, 3.14159265358979323846 / 6.0, 1e-15) && Near(t.depth_walk, 0.2879793265790644, 1e-12) && Near(t.area, 0.2591813939211579, 1e-12), "spiral: 30 degree treads, walkline depth 0.28798, area 0.25918");
  d8test::Check(Near(12 * t.area, 3.1101767270538954, 1e-12), "spiral: twelve treads fill the annulus pi (R^2 - r^2)");
  d8test::Check(Near(t.depth_inner, 0.1 * t.angle_rad) && Near(t.depth_outer, 1.0 * t.angle_rad) && t.depth_inner < t.depth_walk && t.depth_walk < t.depth_outer, "spiral: depth grows with radius");
  double num = 0;
  const int nr = 20000;
  for (int i = 0; i < nr; ++i) {
    const double rho = 0.1 + 0.9 * (i + 0.5) / nr;
    num += rho * t.angle_rad * (0.9 / nr);
  }
  d8test::Check(Near(num, t.area, 1e-9), "spiral: tread area equals the integral of arc depth over radius");
  const SectorTread lw = SectorTreadGeometry(90.0, 3, 0.0, 1.0, 0.6), uw = SectorTreadGeometry(180.0, 6, 0.0, 1.0, 0.6);
  d8test::Check(Near(lw.angle_rad * 3 * 180.0 / 3.14159265358979323846, 90.0, 1e-9) && Near(uw.angle_rad * 6 * 180.0 / 3.14159265358979323846, 180.0, 1e-9) && Near(lw.angle_rad, uw.angle_rad, 1e-15), "winders: L corner 3 x 30 degrees, U turn 6 x 30 degrees");
  d8test::Check(Near(lw.depth_walk, 0.6 * 3.14159265358979323846 / 6.0, 1e-12) && Near(3 * lw.area, 0.5 * (3.14159265358979323846 / 2.0), 1e-12), "winders: walkline depth 0.6 pi/6 and the three treads fill a quarter disc");
  d8test::Check(StraightRunTreads(14) == 13 && StraightRunTreads(0) == 0 && SectorTreadGeometry(360, 0, 0, 1, 0.5).area == 0, "stairs: straight run of n risers has n-1 treads; no treads, no geometry");
}

// D8-REVIT-ARCH-0068 (area part): "Create an area ... by clicking inside area boundary lines so it expands to their extents".
// Outer 10 x 8 with a 2 x 3 hole: 80 - 6 = 74, perimeter of the outer loop 36. L shape (0,0)(4,0)(4,2)(2,2)(2,4)(0,4): area 12 (4x2 + 2x2),
// centroid by the two rectangles ((8*2 + 4*1)/12, (8*1 + 4*3)/12) = (1.6667, 1.6667); the clockwise listing gives the same.
D8_EXTRA_TEST(Rw3AreaFromBoundaryLoops) {
  const std::vector<P2> outer = {{0, 0}, {10, 0}, {10, 8}, {0, 8}}, hole = {{4, 2}, {6, 2}, {6, 5}, {4, 5}};
  d8test::Check(Near(AreaWithHoles(outer, {hole}), 74.0) && Near(PolygonPerimeter(outer), 36.0) && Near(AreaWithHoles(outer, {}), 80.0), "area: 10 x 8 minus a 2 x 3 hole is 74, boundary 36");
  const std::vector<P2> lshape = {{0, 0}, {4, 0}, {4, 2}, {2, 2}, {2, 4}, {0, 4}};
  std::vector<P2> rev(lshape.rbegin(), lshape.rend());
  const P2 c = PolygonCentroid(lshape), cr = PolygonCentroid(rev);
  d8test::Check(Near(PolygonArea(lshape), 12.0) && Near(PolygonArea(rev), 12.0) && Near(SignedArea(lshape), 12.0) && Near(SignedArea(rev), -12.0), "area: L shape 12 independent of winding");
  d8test::Check(Near(c.x, 20.0 / 12.0, 1e-12) && Near(c.y, 20.0 / 12.0, 1e-12) && Near(cr.x, c.x, 1e-12) && Near(cr.y, c.y, 1e-12), "area: L-shape centroid (5/3, 5/3)");
}

// D8-REVIT-ARCH-0083: "Constrain room height by a base offset and upper limit level or offset."
// Base level 0 + 0.15, upper limit level 3.0 - 0.2: height 2.65; floor area 12 gives 31.8. Limit below the base gives zero volume.
D8_EXTRA_TEST(Rw3RoomHeightLimitsVolume) {
  RoomLimits l;
  l.base_level = 0;
  l.base_offset = 0.15;
  l.limit_level = 3.0;
  l.limit_offset = -0.2;
  d8test::Check(Near(l.Height(), 2.65) && Near(RoomVolume(12.0, l), 31.8, 1e-12), "room height: 3.0 - 0.2 - 0.15 = 2.65, volume 31.8");
  l.base_level = 3.0;
  l.base_offset = 0.5;
  d8test::Check(Near(l.Height(), -0.7) && RoomVolume(12.0, l) == 0.0, "room height: limit below the base gives no volume");
  RoomLimits u;
  u.limit_level = 6.0;
  d8test::Check(Near(RoomVolume(5.0, u), 30.0), "room height: no offsets, level 0 to 6 over 5 m2 is 30");
}

// D8-REVIT-ARCH-0130: "Place a room in every enclosed region of a plan with a single command."
// Grid of three horizontal (y=0,1,2) and three vertical (x=0,1,2) unit-spaced lines of length 2: V=9, E=12, so by Euler's formula
// the bounded faces number E - V + 1 = 4, each of area 1. A 2 x 1 box with a separator across it: 2 regions of area 1 (1 region of area 2
// without). Two long lines crossing inside a square without sharing endpoints: 4 triangles of area 1. A box with a missing side: none.
D8_EXTRA_TEST(Rw3RoomsInEveryEnclosedRegion) {
  std::vector<Segment2> grid;
  for (int k = 0; k <= 2; ++k) {
    grid.push_back({{0.0, (double)k}, {2.0, (double)k}});
    grid.push_back({{(double)k, 0.0}, {(double)k, 2.0}});
  }
  std::vector<Region> r = BoundedRegions(grid);
  const int V = 9, E = 12;
  d8test::Check((int)r.size() == E - V + 1, "regions: a 2 x 2 grid of lines has E - V + 1 = 4 bounded regions (Euler)");
  double total = 0;
  bool all_one = true;
  for (const Region& g : r) {
    total += g.area;
    all_one = all_one && Near(g.area, 1.0, 1e-9);
  }
  d8test::Check(all_one && Near(total, 4.0, 1e-9), "regions: each grid cell has area 1");
  d8test::Check(Near(r[0].centroid.x, 0.5, 1e-9) && Near(r[0].centroid.y, 0.5, 1e-9) && Near(r[3].centroid.x, 1.5, 1e-9) && Near(r[3].centroid.y, 1.5, 1e-9), "regions: centroids are the cell centres");
  std::vector<Segment2> box = {{{0, 0}, {2, 0}}, {{2, 0}, {2, 1}}, {{2, 1}, {0, 1}}, {{0, 1}, {0, 0}}};
  r = BoundedRegions(box);
  d8test::Check(r.size() == 1 && Near(r[0].area, 2.0, 1e-9), "regions: one closed box is one region of area 2");
  box.push_back({{1, 0}, {1, 1}});
  r = BoundedRegions(box);
  d8test::Check(r.size() == 2 && Near(r[0].area, 1.0, 1e-9) && Near(r[1].area, 1.0, 1e-9), "regions: a separation line splits the box into two rooms of area 1");
  box.push_back({{0, 0.5}, {0.5, 0.5}});
  r = BoundedRegions(box);
  d8test::Check(r.size() == 2 && Near(r[0].area + r[1].area, 2.0, 1e-9), "regions: a dangling wall stub creates no room and removes no area");
  std::vector<Segment2> cross = {{{0, 0}, {2, 0}}, {{2, 0}, {2, 2}}, {{2, 2}, {0, 2}}, {{0, 2}, {0, 0}}, {{0, 0}, {2, 2}}, {{2, 0}, {0, 2}}};
  r = BoundedRegions(cross);
  double ta = 0;
  for (const Region& g : r) ta += g.area;
  d8test::Check(r.size() == 4 && Near(ta, 4.0, 1e-9) && Near(r[0].area, 1.0, 1e-9), "regions: crossing diagonals split a square into 4 triangles of area 1");
  std::vector<Segment2> open = {{{0, 0}, {2, 0}}, {{2, 0}, {2, 1}}, {{0, 1}, {0, 0}}};
  d8test::Check(BoundedRegions(open).empty() && BoundedRegions({}).empty(), "regions: an open U shape and an empty set have no region");
}

// D8-REVIT-ARCH-0121: "Window sill and head heights are parameters with schedule output."
// Type A 1.2 x 1.5 at sill 0.9 (two of them): head 0.9 + 1.5 = 2.4, area 1.8 each, 3.6 total. Type B 0.6 x 0.6 at sills 1.5 and 1.2: heads 2.1
// and 1.8, areas 0.36 each, 0.72 total; minimum sill 1.2, maximum head 2.1. Rows come out sorted by type name.
D8_EXTRA_TEST(Rw3WindowScheduleSillHead) {
  const std::vector<OpeningRow> rows = {{"W1", "A", 1.2, 1.5, 0.9}, {"W3", "B", 0.6, 0.6, 1.5}, {"W2", "A", 1.2, 1.5, 0.9}, {"W4", "B", 0.6, 0.6, 1.2}};
  d8test::Check(Near(rows[0].Head(), 2.4) && Near(rows[1].Head(), 2.1) && Near(rows[3].Head(), 1.8) && Near(rows[0].Area(), 1.8), "window schedule: head = sill + height, area = width x height");
  const std::vector<OpeningTypeTotal> t = ScheduleByType(rows);
  d8test::Check(t.size() == 2 && t[0].type == "A" && t[1].type == "B", "window schedule: two types, sorted");
  d8test::Check(t[0].count == 2 && Near(t[0].total_area, 3.6) && Near(t[0].min_sill, 0.9) && Near(t[0].max_head, 2.4), "window schedule: type A totals");
  d8test::Check(t[1].count == 2 && Near(t[1].total_area, 0.72) && Near(t[1].min_sill, 1.2) && Near(t[1].max_head, 2.1), "window schedule: type B totals");
  d8test::Check(ScheduleByType({}).empty(), "window schedule: no windows, no rows");
}

// D8-REVIT-ARCH-0132 (swing part): "Doors show in plan with a hinge side and swing direction that can be flipped by hand and facing
// controls." (tags and numbering are not tested)
// Wall along x from 1 to 2 (width 1). Hinge left, swing +y, open 90 degrees: hinge (1,0), tip (1,1); closed tip (2,0). Flip hand: hinge
// (2,0), tip (2,1). Flip facing: tip (1,-1). Both flips: hinge (2,0), tip (2,-1). Open 60 degrees tip (1.5, 0.8660254). Swing area
// 0.5 * (pi/2) * 1 = pi/4 at 90 degrees.
D8_EXTRA_TEST(Rw3DoorHingeSwingFlip) {
  DoorLeaf d;
  d.wall_x0 = 1.0;
  d.width = 1.0;
  P2 tip = DoorLeafTip(d, 90.0), closed = DoorLeafTip(d, 0.0);
  d8test::Check(Near(DoorHinge(d).x, 1.0) && Near(tip.x, 1.0, 1e-12) && Near(tip.y, 1.0, 1e-12) && Near(closed.x, 2.0) && Near(closed.y, 0.0, 1e-12), "door swing: left hinge opens to (1,1), closed tip at (2,0)");
  const DoorLeaf h = FlipHand(d), f = FlipFacing(d), hf = FlipFacing(FlipHand(d));
  d8test::Check(Near(DoorHinge(h).x, 2.0) && Near(DoorLeafTip(h, 90).x, 2.0, 1e-12) && Near(DoorLeafTip(h, 90).y, 1.0, 1e-12), "door swing: flipping the hand moves the hinge to the other jamb");
  d8test::Check(Near(DoorLeafTip(f, 90).x, 1.0, 1e-12) && Near(DoorLeafTip(f, 90).y, -1.0, 1e-12), "door swing: flipping the facing swings to the other side");
  d8test::Check(Near(DoorLeafTip(hf, 90).x, 2.0, 1e-12) && Near(DoorLeafTip(hf, 90).y, -1.0, 1e-12) && Near(DoorLeafTip(FlipHand(h), 90).x, 1.0, 1e-12), "door swing: both flips combine, and a double flip returns");
  const P2 t60 = DoorLeafTip(d, 60.0);
  d8test::Check(Near(t60.x, 1.5, 1e-12) && Near(t60.y, 0.8660254037844386, 1e-12) && Near(DoorSwingArea(d, 90.0), 0.7853981633974483, 1e-12), "door swing: 60 degree tip (1.5, 0.866), 90 degree swing area pi/4");
  d8test::Check(Near(std::hypot(t60.x - 1.0, t60.y), 1.0, 1e-12), "door swing: the leaf keeps its length at every angle");
}

// D8-ARCHICAD-PROPS-0036: "Let expressions compute zone-based quantities, such as a window glazing ratio against the zone's net area,
// to show regulation compliance per element." (expression evaluation and labels are not tested; the ratio and the comparison are)
// Windows 1.8 and 0.36 m2 in a 20 m2 zone: 2.16 / 20 = 0.108; against a minimum of 0.10 it passes, against 0.12 it fails.
D8_EXTRA_TEST(Rw3ZoneGlazingRatio) {
  const double r = GlazingRatio({1.8, 0.36}, 20.0);
  d8test::Check(Near(r, 0.108, 1e-12) && r >= 0.10 && !(r >= 0.12), "glazing ratio: 2.16 / 20 = 0.108, above 0.10 and below 0.12");
  d8test::Check(GlazingRatio({}, 20.0) == 0.0 && GlazingRatio({1.0}, 0.0) == 0.0, "glazing ratio: no windows or a zero zone area give 0");
}

} // namespace
