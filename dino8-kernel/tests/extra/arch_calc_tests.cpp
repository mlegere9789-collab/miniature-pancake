// Architectural design rules and quantities (include/dino8/kernel/arch_calc.h). One test per Register row; every expected value is a
// hand-derived closed form (the derivation is in the comment of each test), never read back from the code under test. Where a
// second independent route exists a brute-force numeric check (point sampling on a grid, trial riser counts) sits beside it.
#include <algorithm>
#include <cmath>
#include <vector>

#include "dino8/kernel/arch_calc.h"
#include "test_registry.h"

namespace {

using namespace dino8::kernel;

bool Near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

// Point-in-polygon by the crossing-number rule, used only by the sampling cross-checks.
bool Inside(const std::vector<ArchPt2>& p, double x, double y) {
  bool in = false;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
    if (((p[i].y > y) != (p[j].y > y)) && (x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x)) in = !in;
  }
  return in;
}

// L-shaped zone: the 6 x 5 rectangle minus its 3 x 3 top-right corner, area 30 - 9 = 21, counter-clockwise.
std::vector<ArchPt2> LShape() { return {{0, 0}, {6, 0}, {6, 2}, {3, 2}, {3, 5}, {0, 5}}; }

}  // namespace

// D8-REVIT-ARCH-0099: maximum riser, minimum tread and minimum run width, plus the Blondel comfort rule.
D8_EXTRA_TEST(ArchStairRulesCheck) {
  StairRules r;  // max riser 0.19, min tread 0.25, min width 1.0, 2R + T in [0.60, 0.66]
  StairCheck ok = CheckStair(0.18, 0.28, 1.10, r);
  d8test::Check(ok.ok && ok.violations.empty(), "stair 0.18 / 0.28 / 1.10 passes every rule");
  d8test::Check(Near(ok.blondel, 0.64), "Blondel value 2*0.18 + 0.28 = 0.64");
  StairCheck limit = CheckStair(0.19, 0.25, 1.00, r);
  d8test::Check(limit.riser_ok && limit.tread_ok && limit.width_ok, "values exactly on the maximum riser, minimum tread and minimum width pass");
  d8test::Check(limit.blondel_ok && Near(limit.blondel, 0.63), "at the limits 2*0.19 + 0.25 = 0.63 lies inside the Blondel range");
  StairCheck high = CheckStair(0.20, 0.28, 1.10, r);
  d8test::Check(!high.ok && !high.riser_ok && high.tread_ok && high.width_ok, "riser 0.20 breaks only the riser limit (its Blondel value 0.68 breaks the comfort range as well)");
  d8test::Check(Near(high.blondel, 0.68) && !high.blondel_ok, "2*0.20 + 0.28 = 0.68 is outside [0.60, 0.66]");
  StairCheck shallow = CheckStair(0.18, 0.24, 1.10, r);
  d8test::Check(!shallow.ok && !shallow.tread_ok && shallow.riser_ok && shallow.blondel_ok && shallow.violations.size() == 1,
                "tread 0.24 breaks exactly the minimum-tread rule (2*0.18 + 0.24 = 0.60 is still comfortable)");
  StairCheck narrow = CheckStair(0.18, 0.28, 0.90, r);
  d8test::Check(!narrow.ok && !narrow.width_ok && narrow.violations.size() == 1, "run width 0.90 breaks exactly the minimum-width rule");
  StairRules strict = r;
  strict.max_riser = 0.17;
  d8test::Check(!CheckStair(0.18, 0.28, 1.10, strict).riser_ok, "the limits are inputs: the same riser fails a stricter maximum of 0.17");
}

// D8-ARCHICAD-TOOLBOX-0110: stair rules and alternative fixes when the input conflicts.
// Total rise 2.80 with the default rules: riser 2.8/n must lie in [0.10, 0.19], so n >= 2.8/0.19 = 14.74 -> 15 and n <= 2.8/0.10 = 28.
// Blondel tread at the range centre 0.63: tread = 0.63 - 2 * 2.8/15 = 0.63 - 0.373333 = 0.256667 (>= 0.25, accepted).
D8_EXTRA_TEST(ArchStairProposeFixes) {
  StairRules r;
  std::vector<StairOption> opts = ProposeStairOptions(2.80, r);
  d8test::Check(opts.size() == 14, "rise 2.80: riser counts 15..28 are the 14 admissible options");
  d8test::Check(!opts.empty() && opts.front().risers == 15 && Near(opts.front().riser, 2.8 / 15.0) && Near(opts.front().tread, 0.63 - 2.0 * 2.8 / 15.0) &&
                    Near(opts.front().run, 14.0 * (0.63 - 2.0 * 2.8 / 15.0)),
                "first option: 15 risers of 0.186667, tread 0.256667, run 14 treads");
  bool every = true, covers = true;
  std::vector<int> counts;
  for (const StairOption& o : opts) {
    every = every && CheckStair(o.riser, o.tread, r.min_width, r).ok && Near(o.riser * o.risers, 2.80);
    counts.push_back(o.risers);
  }
  for (int n = 15; n <= 28; ++n) covers = covers && std::count(counts.begin(), counts.end(), n) == 1;
  d8test::Check(every, "every proposed option passes CheckStair and climbs exactly the total rise");
  d8test::Check(covers, "the options are exactly the riser counts 15..28, none missing, none twice");
  // The conflicting input: 13 risers of 0.2154 breaks the riser rule, and the fix list is non-empty and starts nearest to 13 risers.
  StairCheck bad = CheckStair(2.80 / 13.0, 0.28, 1.0, r);
  d8test::Check(!bad.riser_ok, "13 risers over 2.80 (0.2154) is a conflict");
  std::vector<StairOption> near13 = ProposeStairOptions(2.80, r, 0.28, 13);
  d8test::Check(!near13.empty() && near13.front().risers == 15, "proposed fixes for a 13-riser attempt start at 15 risers (the nearest admissible count)");
  d8test::Check(Near(near13.front().tread, 0.28),
                "the preferred tread 0.28 is kept when it satisfies the rules (2*0.18667 + 0.28 = 0.6533)");
  d8test::Check(ProposeStairOptions(0.0, r).empty() && ProposeStairOptions(-1.0, r).empty(), "no rise, no options");
  d8test::Check(ProposeStairOptions(0.05, r).empty(), "a rise of 0.05 is below even one minimum riser: no options");
}

// D8-ARCHICAD-TOOLBOX-0150: a stair linked to a story keeps its riser count; the riser follows the story height and the stair is
// flagged when the rules break. Start: 16 risers, tread 0.28, story 2.88 (riser 0.18).
D8_EXTRA_TEST(ArchStairRelinkStory) {
  StairRules r;
  StairRelink small = RelinkStairToStory(16, 0.28, 2.96, r);  // riser 0.185, 2R + T = 0.65
  d8test::Check(small.valid && Near(small.riser, 0.185) && Near(small.blondel, 0.65) && small.reasons.empty() && small.fixes.empty(),
                "a small story-height change (2.88 -> 2.96) rescales the riser to 0.185 and stays valid");
  StairRelink big = RelinkStairToStory(16, 0.28, 3.20, r);  // riser 0.20 > 0.19
  d8test::Check(!big.valid && Near(big.riser, 0.20) && !big.reasons.empty(), "a large change (story 3.20, riser 0.20) is flagged invalid with a reason");
  d8test::Check(!big.fixes.empty() && big.fixes.front().risers == 17 && Near(big.fixes.front().riser, 3.2 / 17.0) && Near(big.fixes.front().tread, 0.28),
                "the first fix is the nearest riser count, 17 (riser 0.188235), keeping the tread 0.28 (2*0.188235 + 0.28 = 0.6565 <= 0.66)");
  StairRelink low = RelinkStairToStory(16, 0.28, 2.00, r);  // riser 0.125, 2R + T = 0.53 < 0.60
  d8test::Check(!low.valid && Near(low.riser, 0.125) && Near(low.blondel, 0.53), "a story lowered to 2.00 breaks the comfort rule (0.53)");
  d8test::Check(!low.fixes.empty() && low.fixes.front().risers == 16 && Near(low.fixes.front().tread, 0.63 - 0.25, 1e-12),
                "the fix keeps 16 risers and lengthens the tread to 0.63 - 2*0.125 = 0.38");
  d8test::Check(!RelinkStairToStory(0, 0.28, 2.9, r).valid && !RelinkStairToStory(16, 0.28, 0.0, r).valid, "zero risers or no story height is never valid");
}

// D8-REVIT-ARCH-0104: a ramp type with a maximum slope ratio and a maximum run before a landing is required.
// Rise 0.90, max slope 1/12, max run per flight 9.0: minimum horizontal run 0.90 * 12 = 10.8 -> ceil(10.8 / 9) = 2 flights of 5.4,
// one landing; with a 1.5 landing the footprint is 10.8 + 1.5 = 12.3.
D8_EXTRA_TEST(ArchRampPlanSlopeAndLanding) {
  RampPlan p = PlanRamp(0.90, 1.0 / 12.0, 9.0, 1.5);
  d8test::Check(p.ok && p.flights == 2 && p.landings == 1, "rise 0.90 at 1:12 with 9.0 max run needs 2 flights and 1 landing");
  d8test::Check(Near(p.run_per_flight, 5.4) && Near(p.total_run, 10.8) && Near(p.total_length, 12.3) && Near(p.slope, 1.0 / 12.0, 1e-12),
                "flights of 5.4, total run 10.8, footprint 12.3, slope exactly 1:12");
  RampPlan single = PlanRamp(0.45, 1.0 / 12.0, 9.0, 1.5);  // run 5.4 <= 9 -> one flight, no landing
  d8test::Check(single.ok && single.flights == 1 && single.landings == 0 && Near(single.total_length, 5.4), "rise 0.45 fits one flight of 5.4 with no landing");
  RampPlan exact = PlanRamp(0.75, 1.0 / 12.0, 9.0, 1.0);  // run 9.0 exactly equals the maximum: still one flight
  d8test::Check(exact.ok && exact.flights == 1, "a run exactly equal to the maximum does not need a landing");
  d8test::Check(!PlanRamp(0.0, 0.1, 9.0, 1.0).ok && !PlanRamp(1.0, 0.0, 9.0, 1.0).ok, "no rise or no allowed slope is not a plan");
  RampCheck good = CheckRamp(0.45, 5.4, 2, 1.0 / 12.0, 9.0);
  d8test::Check(good.ok && Near(good.slope, 1.0 / 12.0, 1e-12), "the planned flights pass the check (slope on the limit)");
  RampCheck steep = CheckRamp(0.60, 5.4, 2, 1.0 / 12.0, 9.0);  // 0.1111 > 0.0833
  d8test::Check(!steep.ok && Near(steep.slope, 0.60 / 5.4) && steep.violations.size() == 1, "a 0.60 rise over 5.4 (slope 0.111) exceeds 1:12");
  RampCheck longrun = CheckRamp(0.45, 9.5, 1, 1.0 / 12.0, 9.0);  // slope 0.0474 fine, run 9.5 > 9.0
  d8test::Check(!longrun.ok && longrun.violations.size() == 1, "a 9.5 flight is gentle enough but longer than the 9.0 allowed before a landing");
}

// D8-ARCHICAD-TOOLBOX-0141: pitch in degrees, percent or rise per run; negative pitches; pitch from a typed elevation.
// tan 30 deg = 1/sqrt(3) = 0.5773502691896257; atan(1/3) = 18.43494882292201 deg; atan(0.5) = 26.56505117707799 deg.
D8_EXTRA_TEST(ArchRoofPitchNotation) {
  RoofPitch a = PitchFromDegrees(30.0);
  d8test::Check(a.valid && Near(a.percent, 57.73502691896257, 1e-9) && Near(a.rise_per_run, 0.5773502691896257, 1e-12), "30 degrees = 57.735 percent = 0.57735 rise per run");
  RoofPitch b = PitchFromPercent(100.0);
  d8test::Check(b.valid && Near(b.degrees, 45.0, 1e-12) && Near(b.rise_per_run, 1.0, 1e-12), "100 percent = 45 degrees");
  RoofPitch c = PitchFromRiseRun(4.0, 12.0);
  d8test::Check(c.valid && Near(c.degrees, 18.43494882292201, 1e-9) && Near(c.percent, 100.0 / 3.0, 1e-9), "4 in 12 = 18.4349 degrees = 33.33 percent");
  RoofPitch d = PitchFromDegrees(-45.0);
  d8test::Check(d.valid && Near(d.percent, -100.0, 1e-9) && Near(d.rise_per_run, -1.0, 1e-12), "a negative pitch slopes down: -45 degrees = -100 percent");
  RoofPitch e = PitchFromElevation(3.0, 4.5, 3.0);  // rises 1.5 over 3.0 of run
  d8test::Check(e.valid && Near(e.degrees, 26.56505117707799, 1e-9) && Near(e.rise_per_run, 0.5, 1e-12), "typing elevation 4.5 at 3.0 from an eave at 3.0 gives pitch 26.565 degrees");
  RoofPitch f = PitchFromElevation(3.0, 2.0, 2.0);  // drops 1.0 over 2.0
  d8test::Check(f.valid && Near(f.rise_per_run, -0.5, 1e-12) && f.degrees < 0, "an elevation below the eave gives a negative pitch");
  d8test::Check(!PitchFromDegrees(90.0).valid && !PitchFromDegrees(-90.0).valid && !PitchFromRiseRun(1.0, 0.0).valid && !PitchFromElevation(0, 1, 0).valid,
                "a vertical plane or a zero run is not a pitch");
  RoofPitch rt = PitchFromPercent(PitchFromDegrees(37.0).percent);
  d8test::Check(Near(rt.degrees, 37.0, 1e-9), "degrees -> percent -> degrees round-trips");
}

// D8-ARCHICAD-TOOLBOX-0063: zone area by shoelace, and net area / volume after the elements that bound or reduce it.
// L-shape: 6 x 5 minus 3 x 3 = 21 (also 6*2 + 3*3 = 21 by splitting into two rectangles). Shoelace of the 6 vertices:
//   sum(x_i*y_{i+1} - x_{i+1}*y_i) = 42 (terms listed below), area 21.
D8_EXTRA_TEST(ArchZoneAreaVolume) {
  std::vector<ArchPt2> L = LShape();
  // Terms: (0,0)-(6,0): 0; (6,0)-(6,2): 12; (6,2)-(3,2): 6*2 - 3*2 = 6; (3,2)-(3,5): 15 - 6 = 9; (3,5)-(0,5): 15 - 0 = 15; (0,5)-(0,0): 0.
  // Sum 0 + 12 + 6 + 9 + 15 + 0 = 42 -> signed area 21.
  d8test::Check(Near(PolygonAreaSigned(L), 21.0, 1e-12), "L-shaped zone: counter-clockwise shoelace area is +21");
  std::vector<ArchPt2> cw(L.rbegin(), L.rend());
  d8test::Check(Near(PolygonAreaSigned(cw), -21.0, 1e-12) && Near(PolygonArea(cw), 21.0, 1e-12), "the reversed vertex order gives -21 and the absolute area 21");
  // Independent route: sample the plane on a 0.01 grid and count the cell centres inside (error bounded by perimeter * step).
  long hits = 0;
  const double step = 0.01;
  for (double y = step / 2; y < 5.0; y += step)
    for (double x = step / 2; x < 6.0; x += step) hits += Inside(L, x, y) ? 1 : 0;
  d8test::Check(std::fabs(hits * step * step - 21.0) < 22.0 * step, "a 0.01 grid of sample points confirms the area 21 within the perimeter-times-step bound");
  // Elements: a 0.5 wall stub (Subtract), a 0.3 pilaster that only reduces area, two 0.16 columns (Subtract) below a 0.2 minimum.
  std::vector<ZoneElement> els = {{0.5, ZoneEffect::Subtract}, {0.3, ZoneEffect::ReduceArea}, {0.16, ZoneEffect::Subtract}, {0.16, ZoneEffect::Subtract}, {0.4, ZoneEffect::None}};
  ZoneQuantities q = ComputeZone(L, 2.5, els, 0.2);
  d8test::Check(Near(q.gross_area, 21.0, 1e-12) && q.elements_counted == 2, "with a 0.2 minimum only the 0.5 stub and the 0.3 pilaster count (columns of 0.16 and the None element do not)");
  d8test::Check(Near(q.net_area, 21.0 - 0.5 - 0.3, 1e-12), "net area = 21 - 0.5 - 0.3 = 20.2");
  d8test::Check(Near(q.volume, (21.0 - 0.5) * 2.5, 1e-12), "volume = (21 - 0.5) * 2.5 = 51.25: a ReduceArea element does not reduce the volume");
  ZoneQuantities all = ComputeZone(L, 2.5, els, 0.0);
  d8test::Check(all.elements_counted == 4 && Near(all.net_area, 21.0 - 0.5 - 0.3 - 0.32, 1e-12) && Near(all.volume, (21.0 - 0.5 - 0.32) * 2.5, 1e-12),
                "with no minimum the two columns count too: net 19.88, volume 20.18 * 2.5 = 50.45");
}

// D8-ARCHICAD-TOOLBOX-0117: wall recesses added to the zone area when they exceed a minimum depth and/or area.
D8_EXTRA_TEST(ArchZoneRecessAdd) {
  std::vector<WallRecess> rec = {{0.15, 0.30}, {0.08, 0.50}, {0.20, 0.10}, {0.05, 0.05}};
  // Limits depth >= 0.10, area >= 0.25. AND: only (0.15, 0.30) -> 0.30. OR: 0.30 (both) + 0.50 (area) + 0.10 (depth) = 0.90.
  d8test::Check(Near(RecessAddedArea(rec, 0.10, 0.25, RecessRule::DepthAndArea), 0.30, 1e-12), "AND rule: only the recess that is both deep and large adds 0.30");
  d8test::Check(Near(RecessAddedArea(rec, 0.10, 0.25, RecessRule::DepthOrArea), 0.90, 1e-12), "OR rule: deep or large recesses add 0.30 + 0.50 + 0.10 = 0.90");
  std::vector<WallRecess> edge = {{0.10, 0.25}};
  d8test::Check(Near(RecessAddedArea(edge, 0.10, 0.25, RecessRule::DepthAndArea), 0.25, 1e-12), "a recess exactly on both minimums is added (the limits are inclusive)");
  d8test::Check(RecessAddedArea({}, 0.1, 0.1, RecessRule::DepthOrArea) == 0.0, "no recesses add nothing");
  d8test::Check(Near(RecessAddedArea(rec, 0.0, 0.0, RecessRule::DepthAndArea), 0.95, 1e-12), "zero minimums add every recess: 0.30 + 0.50 + 0.10 + 0.05 = 0.95");
}

// D8-ARCHICAD-TOOLBOX-0064: area reduced under low ceilings with up to two height limits and a subtraction percentage each.
// Rectangle 6 x 4 (area 24), ceiling h = 3.0 - 0.3 x. h < 2.4 <=> x > 2: area 4 * 4 = 16. h < 1.5 <=> x > 5: area 1 * 4 = 4.
//   counted = 24 - 0.5 * (16 - 4) - 1.0 * 4 = 24 - 6 - 4 = 14.   Single limit: 24 - 0.5 * 16 = 16.
// Tilted plane h = 3.0 - 0.2 x - 0.1 y, limit 2.5: h >= 2.5 <=> x < 2.5 - 0.5 y, area int_0^4 (2.5 - 0.5 y) dy = 10 - 4 = 6, so 18 is below.
// L-shape with h = 3.0 - 0.3 x, limit 2.4 (x > 2): strip y in [0, 2] gives 4 * 2 = 8, upper arm x in (2, 3), y in [2, 5] gives 1 * 3 = 3: 11.
D8_EXTRA_TEST(ArchZoneLowCeilingReduction) {
  std::vector<ArchPt2> rect = {{0, 0}, {6, 0}, {6, 4}, {0, 4}};
  LowCeilingRule two;
  two.limit1 = 2.4; two.pct1 = 0.5; two.two_limits = true; two.limit2 = 1.5; two.pct2 = 1.0;
  LowCeilingResult r2 = ZoneAreaUnderLowCeilings(rect, 3.0, -0.3, 0.0, two);
  d8test::Check(Near(r2.full_area, 24.0, 1e-12) && Near(r2.area_below1, 16.0, 1e-9) && Near(r2.area_below2, 4.0, 1e-9), "areas under 2.4 and under 1.5: 16 and 4");
  d8test::Check(Near(r2.counted_area, 14.0, 1e-9), "two limits: 24 - 0.5 * 12 - 1.0 * 4 = 14");
  LowCeilingRule one;
  one.limit1 = 2.4; one.pct1 = 0.5;
  d8test::Check(Near(ZoneAreaUnderLowCeilings(rect, 3.0, -0.3, 0.0, one).counted_area, 16.0, 1e-9), "one limit: 24 - 0.5 * 16 = 16");
  LowCeilingRule swapped = two;
  std::swap(swapped.limit1, swapped.limit2); std::swap(swapped.pct1, swapped.pct2);
  d8test::Check(Near(ZoneAreaUnderLowCeilings(rect, 3.0, -0.3, 0.0, swapped).counted_area, 14.0, 1e-9), "limits given in the other order give the same result");
  LowCeilingRule tilt;
  tilt.limit1 = 2.5; tilt.pct1 = 1.0;
  LowCeilingResult rt = ZoneAreaUnderLowCeilings(rect, 3.0, -0.2, -0.1, tilt);
  d8test::Check(Near(rt.area_below1, 18.0, 1e-9) && Near(rt.counted_area, 6.0, 1e-9), "tilted ceiling: 18 of the 24 lies under 2.5, a full reduction leaves 6");
  // Cross-check the tilted case by sampling (independent of the half-plane clipping).
  long below = 0;
  const double step = 0.01;
  for (double y = step / 2; y < 4.0; y += step)
    for (double x = step / 2; x < 6.0; x += step) below += (3.0 - 0.2 * x - 0.1 * y < 2.5) ? 1 : 0;
  d8test::Check(std::fabs(below * step * step - 18.0) < 0.05, "a 0.01 sampling grid agrees: 18 under the tilted ceiling");
  LowCeilingRule lone;
  lone.limit1 = 2.4; lone.pct1 = 0.5;
  LowCeilingResult rl = ZoneAreaUnderLowCeilings(LShape(), 3.0, -0.3, 0.0, lone);
  d8test::Check(Near(rl.area_below1, 11.0, 1e-9) && Near(rl.counted_area, 21.0 - 5.5, 1e-9), "concave L-shaped zone: 11 under 2.4, counted 21 - 5.5 = 15.5");
  LowCeilingRule none;
  none.limit1 = 1.0; none.pct1 = 1.0;
  d8test::Check(Near(ZoneAreaUnderLowCeilings(rect, 3.0, -0.3, 0.0, none).counted_area, 24.0, 1e-9), "a limit below the whole ceiling (min 1.2) reduces nothing");
}

// D8-REVIT-ARCH-0082: room area and volume at the wall finish, wall centre, core centre or core boundary.
// Wall layers from the room side: finish 0.02, core 0.20, far finish 0.08 (total 0.30), location line at the core centre (0.12 from the room face).
//   Finish: offset 0.12 -> (8 - 0.24)(6 - 0.24) = 7.76 * 5.76 = 44.6976.   Wall centre: pos 0.15, offset -0.03 -> 8.06 * 6.06 = 48.8436.
//   Core centre: offset 0 -> 48.   Core boundary: pos 0.02, offset 0.10 -> 7.8 * 5.8 = 45.24.   Volumes x 2.7.
// L-shape, every edge moved 0.1 inward: bands 5.8 * 1.8 + 2.8 * 3.0 = 10.44 + 8.4 = 18.84. Bottom edge alone moved 0.2: 21 - 6 * 0.2 = 19.8.
D8_EXTRA_TEST(ArchRoomBoundaryLocation) {
  std::vector<ArchPt2> rect = {{0, 0}, {8, 0}, {8, 6}, {0, 6}};
  LayeredWall w{0.02, 0.20, 0.08, 0.12};
  std::vector<LayeredWall> walls(4, w);
  RoomQuantities fin = ComputeRoom(rect, walls, RoomBoundaryLocation::WallFinish, 2.7);
  RoomQuantities cen = ComputeRoom(rect, walls, RoomBoundaryLocation::WallCenter, 2.7);
  RoomQuantities cc = ComputeRoom(rect, walls, RoomBoundaryLocation::CoreCenter, 2.7);
  RoomQuantities cb = ComputeRoom(rect, walls, RoomBoundaryLocation::CoreBoundary, 2.7);
  d8test::Check(Near(fin.area, 44.6976, 1e-9), "wall finish: 7.76 x 5.76 = 44.6976");
  d8test::Check(Near(cen.area, 48.8436, 1e-9), "wall centre: the room grows past the location line to 8.06 x 6.06 = 48.8436");
  d8test::Check(Near(cc.area, 48.0, 1e-9), "core centre coincides with the location line: 48");
  d8test::Check(Near(cb.area, 45.24, 1e-9), "core boundary: 7.8 x 5.8 = 45.24");
  d8test::Check(Near(fin.volume, 44.6976 * 2.7, 1e-9) && Near(cb.volume, 45.24 * 2.7, 1e-9), "volume is area times the room height 2.7");
  d8test::Check(fin.area < cb.area && cb.area < cc.area && cc.area < cen.area, "the four locations are ordered finish < core boundary < core centre < wall centre");
  std::vector<double> in(6, 0.1);
  std::vector<ArchPt2> inset = OffsetPolygonEdges(LShape(), in);
  d8test::Check(inset.size() == 6 && Near(PolygonArea(inset), 18.84, 1e-9), "L-shaped room inset 0.1 on every edge: area 18.84 (reflex corner mitred)");
  d8test::Check(Near(inset[3].x, 2.9, 1e-9) && Near(inset[3].y, 1.9, 1e-9), "the reflex corner (3, 2) moves to (2.9, 1.9)");
  std::vector<ArchPt2> lshape = LShape();
  std::vector<ArchPt2> cw(lshape.rbegin(), lshape.rend());
  d8test::Check(Near(PolygonArea(OffsetPolygonEdges(cw, in)), 18.84, 1e-9), "a clockwise polygon is offset into its interior as well");
  std::vector<double> bottom = {0.2, 0, 0, 0, 0, 0};
  d8test::Check(Near(PolygonArea(OffsetPolygonEdges(LShape(), bottom)), 19.8, 1e-9), "moving only the bottom edge 0.2 inward removes 6 x 0.2 = 1.2");
  d8test::Check(OffsetPolygonEdges(LShape(), {0.1}).empty(), "a wrong number of offsets gives no polygon");
}

// D8-ARCHICAD-PROPS-0014: net and gross volumes and areas of a wall, openings subtracted or ignored.
// Wall 6 long, 2.8 high, 0.3 thick. Door x 1..1.9, 0..2.1 = 0.9 * 2.1 = 1.89; window x 3..4.2, sill 0.9, 1.2 high = 1.2 * 1.2 = 1.44.
//   Gross face 6 * 2.8 = 16.8, openings 3.33, net face 13.47. Volumes x 0.3: gross 5.04, net 4.041.
// Partly outside: a door at x = -0.2 (0.9 wide) keeps 0.7 * 2.1 = 1.47; a window sill 2.5, 1.0 high under the 2.8 top keeps 0.3 of height: 1.2 * 0.3 = 0.36.
D8_EXTRA_TEST(ArchWallNetGrossQuantities) {
  std::vector<WallOpening> ops = {{1.0, 0.9, 0.0, 2.1}, {3.0, 1.2, 0.9, 1.2}};
  WallQuantitiesResult q = ComputeWallQuantities(6.0, 2.8, 2.8, 0.3, ops);
  d8test::Check(q.ok && q.openings_counted == 2, "two separate openings are accepted");
  d8test::Check(Near(q.gross_side_area, 16.8, 1e-12) && Near(q.openings_area, 3.33, 1e-12) && Near(q.net_side_area, 13.47, 1e-12), "face areas: gross 16.8, openings 3.33, net 13.47");
  d8test::Check(Near(q.gross_volume, 5.04, 1e-12) && Near(q.net_volume, 4.041, 1e-12), "volumes: gross 5.04, net 4.041 (thickness 0.3)");
  d8test::Check(Near(q.gross_volume - q.net_volume, 3.33 * 0.3, 1e-12), "gross - net equals the opening area times the thickness");
  std::vector<WallOpening> out = {{-0.2, 0.9, 0.0, 2.1}, {3.0, 1.2, 2.5, 1.0}};
  WallQuantitiesResult c = ComputeWallQuantities(6.0, 2.8, 2.8, 0.3, out);
  d8test::Check(Near(c.openings_area, 1.47 + 0.36, 1e-12), "openings are clipped to the wall: 0.7 * 2.1 + 1.2 * 0.3 = 1.83");
  d8test::Check(Near(c.net_volume, (16.8 - 1.83) * 0.3, 1e-12), "net volume uses only the clipped part: 14.97 * 0.3 = 4.491");
  WallQuantitiesResult none = ComputeWallQuantities(6.0, 2.8, 2.8, 0.3, {});
  d8test::Check(Near(none.net_volume, none.gross_volume, 1e-12) && none.openings_counted == 0, "without openings net equals gross");
}

// D8-ARCHICAD-SEO-0026: element quantities reported net (reduced by openings) and gross (ignoring them) so a schedule can use either;
// overlapping openings would be double-counted and are flagged.
// Wall 10 long, 3.0 high, 0.25 thick: gross volume 7.5. Two windows 1.5 x 1.2 = 1.8 each and a door 1.0 x 2.1 = 2.1: openings 5.7,
// net face 30 - 5.7 = 24.3, net volume 6.075.
D8_EXTRA_TEST(ArchWallSolidOperationQuantities) {
  std::vector<WallOpening> ops = {{1.0, 1.5, 0.9, 1.2}, {4.0, 1.0, 0.0, 2.1}, {6.5, 1.5, 0.9, 1.2}};
  WallQuantitiesResult q = ComputeWallQuantities(10.0, 3.0, 3.0, 0.25, ops);
  d8test::Check(q.ok && q.openings_counted == 3, "three separate openings");
  d8test::Check(Near(q.gross_volume, 7.5, 1e-12) && Near(q.net_volume, 6.075, 1e-12), "gross volume 7.5, net volume 6.075");
  d8test::Check(Near(q.gross_side_area, 30.0, 1e-12) && Near(q.net_side_area, 24.3, 1e-12), "gross face 30, net face 24.3");
  // Net is a schedule choice: the same call yields both numbers and their difference is exactly the opening volume.
  d8test::Check(Near(q.gross_volume - q.net_volume, 5.7 * 0.25, 1e-12), "gross - net = 5.7 * 0.25 = 1.425");
  std::vector<WallOpening> overlap = {{1.0, 1.5, 0.9, 1.2}, {1.5, 1.5, 1.0, 1.0}};  // share x 1.5..2.5, z 1.0..2.1
  d8test::Check(!ComputeWallQuantities(10.0, 3.0, 3.0, 0.25, overlap).ok, "two overlapping openings are flagged (their union would be double-counted)");
  std::vector<WallOpening> touch = {{1.0, 1.0, 0.0, 2.1}, {2.0, 1.0, 0.0, 2.1}};  // adjacent, sharing only an edge
  d8test::Check(ComputeWallQuantities(10.0, 3.0, 3.0, 0.25, touch).ok, "two openings that only touch along an edge are not an overlap");
  std::vector<WallOpening> degenerate = {{1.0, 0.0, 0.0, 2.1}, {2.0, 1.0, 0.0, 0.0}};
  d8test::Check(ComputeWallQuantities(10.0, 3.0, 3.0, 0.25, degenerate).openings_counted == 0, "zero-width or zero-height openings subtract nothing");
}

// D8-ARCHICAD-TOOLBOX-0005: a wall whose top edge slopes between a start and an end height.
// Length 8, heights 2.0 -> 4.0, thickness 0.2: face = 8 * (2 + 4) / 2 = 24, volume 4.8. Top(x) = 2 + x / 4.
//   Opening A x 2..4, sill 0.5, 1.0 high (top 1.5 < top(2) = 2.5): 2 * 1.0 = 2.
//   Opening B x 6..8, sill 3.0, 2.0 high, cut by the sloped top: int_6^8 (2 + x/4 - 3) dx = [x^2/8 - x]_6^8 = (8 - 8) - (4.5 - 6) = 1.5.
//   Net face 24 - 2 - 1.5 = 20.5, net volume 4.1.
D8_EXTRA_TEST(ArchWallSlopedTop) {
  WallQuantitiesResult g = ComputeWallQuantities(8.0, 2.0, 4.0, 0.2, {});
  d8test::Check(Near(g.gross_side_area, 24.0, 1e-12) && Near(g.gross_volume, 4.8, 1e-12), "sloped wall 2.0 -> 4.0 over 8: face 24, volume 4.8");
  std::vector<WallOpening> ops = {{2.0, 2.0, 0.5, 1.0}, {6.0, 2.0, 3.0, 2.0}};
  WallQuantitiesResult q = ComputeWallQuantities(8.0, 2.0, 4.0, 0.2, ops);
  d8test::Check(q.ok && q.openings_counted == 2, "both openings count");
  d8test::Check(Near(q.openings_area, 3.5, 1e-12), "opening areas 2 + 1.5 (the second is cut by the sloped top)");
  d8test::Check(Near(q.net_side_area, 20.5, 1e-12) && Near(q.net_volume, 4.1, 1e-12), "net face 20.5, net volume 4.1");
  WallQuantitiesResult dn = ComputeWallQuantities(8.0, 4.0, 2.0, 0.2, ops);
  d8test::Check(Near(dn.gross_volume, 4.8, 1e-12), "the gross volume of the mirrored slope (4.0 -> 2.0) is the same");
  // Sampling cross-check of opening B (independent of the break-point integration): area where 3 <= z <= min(5, top(x)), x in 6..8.
  double sum = 0;
  const int N = 200000;
  for (int i = 0; i < N; ++i) {
    const double x = 6.0 + 2.0 * (i + 0.5) / N;
    const double top = 2.0 + x / 4.0;
    sum += std::max(0.0, std::min(5.0, top) - 3.0) * (2.0 / N);
  }
  d8test::Check(Near(sum, 1.5, 1e-6), "midpoint sampling of the cut opening agrees: 1.5");
}
