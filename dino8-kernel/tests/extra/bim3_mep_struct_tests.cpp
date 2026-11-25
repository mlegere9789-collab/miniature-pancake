// Duct / pipe sizing, pressure loss, tray fill, wire size, panel demand, run splitting, load combinations and load resultants of
// include/dino8/kernel/bim3_calc.h. One test per Register row. Expected values are closed forms (Hagen-Poiseuille, Colebrook residual,
// Hazen-Williams in two unit systems, Ohm's law) or decimal literals computed outside the code under test; sizing choices are also
// checked by trial over the size list. The equations are the standard ones cited in the header; no building-code limit is built in:
// every limit is an input of the test. Selection, undo and UI clauses of the rows are not tested.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "dino8/kernel/bim3_calc.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::bim3;

bool Near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }
bool NearRel(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::max(std::fabs(a), std::fabs(b)); }

// D8-REVIT-SYS-0010: "Size ducts by equal friction, velocity, static regain or fixed constraints." (static regain is not implemented)
// Friction solver checks that do not use the sizing code: laminar flow must give the Hagen-Poiseuille gradient 32 mu V / D^2: D = 0.1,
// Q = 1e-4 -> V = 0.01273240, gradient 7.43572e-4 Pa/m. Colebrook-White: Re = 1e5, eps/D = 1e-4 must satisfy 1/sqrt(f) + 2 log10(eps/3.7D
// + 2.51/(Re sqrt f)) = 0 (f = 0.0185139 from an independent bisection); fully rough limit f = 1/(2 log10(3.7 D/eps))^2 = 0.0379037.
// Equal friction at 1.0 Pa/m, 0.5 m3/s, eps 0.09 mm: exact diameter 0.34025353, so the 0.35 size is chosen (friction 0.8703 Pa/m; the next
// smaller size 0.30 would give 1.861). Velocity 6 m/s: exact 0.325735 -> 0.35 (V = 5.1969). Rectangular 0.4 x 0.3: Huebscher
// equivalent diameter 1.30 (0.12)^0.625 / 0.7^0.25 = 0.377709, hydraulic diameter 2 (0.12)/0.7.
D8_EXTRA_TEST(Rw3DuctSizingFrictionVelocity) {
  const Fluid air;
  const double q_lam = 1e-4, d_lam = 0.1;
  const double v_lam = q_lam / (3.14159265358979323846 * d_lam * d_lam / 4.0);
  d8test::Check(NearRel(FrictionGradient(q_lam, d_lam, air, 0.09e-3), 32.0 * air.mu * v_lam / (d_lam * d_lam), 1e-12) && NearRel(FrictionGradient(q_lam, d_lam, air, 0.09e-3), 7.43571894125335e-4, 1e-9), "duct sizing: laminar gradient equals Hagen-Poiseuille 7.4357e-4 Pa/m");
  const double f = DarcyFriction(1e5, 1e-4);
  d8test::Check(Near(1.0 / std::sqrt(f) + 2.0 * std::log10(1e-4 / 3.7 + 2.51 / (1e5 * std::sqrt(f))), 0.0, 1e-10) && Near(f, 0.018513866077471637, 1e-9), "duct sizing: friction factor solves Colebrook-White (0.018514)");
  d8test::Check(Near(DarcyFriction(1e12, 0.01), 0.03790371189239129, 1e-7) && Near(DarcyFriction(1000.0, 0.001), 0.064, 1e-15) && DarcyFriction(0, 0.001) == 0.0, "duct sizing: fully rough limit 0.037904, laminar 64/Re, zero flow zero factor");
  const std::vector<double> sizes = {0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50};
  const DuctSize e = SizeRoundDuctEqualFriction(0.5, 1.0, air, 0.09e-3, sizes);
  d8test::Check(e.ok && Near(e.exact, 0.3402535281020701, 1e-6) && Near(e.selected, 0.35) && e.friction <= 1.0 && Near(e.friction, 0.8702644720361923, 1e-6) && Near(e.velocity, 5.196896100959848, 1e-6), "duct sizing: equal friction 1.0 Pa/m picks 0.35 (exact 0.34025)");
  double best = 0;
  for (double s : sizes)
    if (FrictionGradient(0.5, s, air, 0.09e-3) <= 1.0 && (best == 0 || s < best)) best = s;
  d8test::Check(Near(best, e.selected), "duct sizing: trial over the list agrees on the smallest size under the friction target");
  d8test::Check(FrictionGradient(0.5, 0.30, air, 0.09e-3) > 1.0 && Near(FrictionGradient(0.5, 0.30, air, 0.09e-3), 1.8614718186091241, 1e-6), "duct sizing: the next smaller size (0.30) would give 1.861 Pa/m");
  const DuctSize v = SizeRoundDuctVelocity(0.5, 6.0, air, 0.09e-3, sizes);
  d8test::Check(v.ok && Near(v.exact, 0.32573500793528, 1e-9) && Near(v.selected, 0.35) && Near(v.velocity, 5.196896100959849, 1e-9), "duct sizing: velocity 6 m/s picks 0.35 (exact 0.325735)");
  d8test::Check(!SizeRoundDuctVelocity(0.5, 6.0, air, 0.09e-3, {0.2, 0.25}).ok && !SizeRoundDuctEqualFriction(0.5, 1.0, air, 0.09e-3, {0.2, 0.25}).ok, "duct sizing: no listed size is large enough");
  d8test::Check(Near(EquivalentDiameterRect(0.4, 0.3), 0.3777088154663358, 1e-12) && Near(EquivalentDiameterRect(0.5, 0.5), 0.5465826699149144, 1e-12) && Near(HydraulicDiameterRect(0.4, 0.3), 0.24 / 0.7, 1e-15), "duct sizing: Huebscher equivalent diameter 0.37771, square 0.54658, hydraulic diameter 0.34286");
}

// D8-REVIT-SYS-0011: "Report pressure loss per section and per system including fittings."
// Section: Q = 0.5, D = 0.4 -> V = 3.97887, dynamic pressure 0.5 * 1.204 * V^2 = 9.530524; straight loss with f = 0.02 over 10 m:
// 0.02 * 10/0.4 * 9.530524 = 4.765262; fittings K = 0.5 + 0.3: 0.8 * 9.530524 = 7.624419; total 12.389681 Pa.
// System tree: sections 0 (root, 5 Pa), 1 (3, child of 0), 2 (4, child of 0), 3 (2, child of 1): paths 0-2 = 9 and 0-1-3 = 10 -> worst path 10.
D8_EXTRA_TEST(Rw3DuctPressureLossSystem) {
  const Fluid air;
  const DuctSection s = {0.5, 0.4, 10.0, 0.8};
  d8test::Check(Near(DynamicPressure(air.rho, VelocityRound(0.5, 0.4)), 9.530523836357396, 1e-9), "pressure loss: dynamic pressure 9.5305 Pa");
  d8test::Check(Near(SectionLoss(s, 0.02, air), 12.389680987264615, 1e-9), "pressure loss: section 4.7653 + 7.6244 = 12.3897 Pa");
  DuctSection straight = s;
  straight.sum_k = 0;
  DuctSection fit = s;
  fit.length = 0;
  d8test::Check(Near(SectionLoss(straight, 0.02, air), 4.765261918178698, 1e-9) && Near(SectionLoss(fit, 0.02, air), 7.624419069085917, 1e-9) && Near(SectionLoss(straight, 0.02, air) + SectionLoss(fit, 0.02, air), SectionLoss(s, 0.02, air), 1e-12), "pressure loss: straight and fitting parts add");
  const WorstPath w = WorstPathLoss({-1, 0, 0, 1}, {5, 3, 4, 2});
  d8test::Check(Near(w.total, 10.0) && w.path == std::vector<int>({0, 1, 3}), "pressure loss: worst path of the tree is sections 0-1-3 with 10 Pa");
  const WorstPath w2 = WorstPathLoss({-1, 0, 0, 1}, {5, 3, 8, 2});
  d8test::Check(Near(w2.total, 13.0) && w2.path == std::vector<int>({0, 2}), "pressure loss: raising section 2 moves the worst path to 0-2");
  const WorstPath single = WorstPathLoss({-1}, {7.5});
  d8test::Check(Near(single.total, 7.5) && single.path.size() == 1 && WorstPathLoss({}, {}).path.empty(), "pressure loss: a single section is its own path; empty system has none");
}

// D8-REVIT-SYS-0027: "Size selected pipe sections from the calculated system flow using a friction criterion, a velocity criterion, or both
// combined with and/or logic, excluding sanitary piping."
// Q = 0.002 m3/s, Hazen-Williams C = 130, inside diameters 25, 32, 40, 50 mm: velocities 4.074, 2.487, 1.592, 1.019 m/s; gradients
// (m/m) 0.8254, 0.2481, 0.08368, 0.02823. Cross-check of the SI formula 10.67 Q^1.852/(C^1.852 D^4.87) against the US form
// 4.52 Q^1.85/(C^1.85 d^4.87) psi/ft (Q gpm, d in) converted to m/m: agree within 0.5 %. Velocity <= 3 only -> 32 mm; friction <= 0.05 only
// -> 50 mm; AND -> 50 mm; OR -> 32 mm.
D8_EXTRA_TEST(Rw3PipeSizingCriteriaAndOr) {
  const std::vector<double> dia = {0.025, 0.032, 0.040, 0.050};
  const double q = 0.002, c = 130;
  const double expect[4] = {0.8254315044606262, 0.24806671095601882, 0.08367904693756399, 0.028227015504794974};
  for (int i = 0; i < 4; ++i) {
    const double us_psi_ft = 4.52 * std::pow(q * 15850.3232, 1.85) / (std::pow(c, 1.85) * std::pow(dia[i] / 0.0254, 4.87));
    const double us_m_per_m = us_psi_ft / 0.3048 * 0.70307;
    d8test::Check(NearRel(HazenWilliamsGradient(q, dia[i], c), expect[i], 1e-9) && NearRel(HazenWilliamsGradient(q, dia[i], c), us_m_per_m, 5e-3), "pipe sizing: Hazen-Williams gradient matches the independent value and the US-unit form");
  }
  PipeCriteria vel;
  vel.use_velocity = true;
  vel.max_velocity = 3.0;
  PipeCriteria fri;
  fri.use_friction = true;
  fri.max_gradient = 0.05;
  PipeCriteria both;
  both.use_velocity = both.use_friction = true;
  both.max_velocity = 3.0;
  both.max_gradient = 0.05;
  PipeCriteria either = both;
  either.logic = CriteriaLogic::Or;
  d8test::Check(SelectPipeSize(q, c, dia, vel) == 1 && SelectPipeSize(q, c, dia, fri) == 3, "pipe sizing: velocity <= 3 m/s gives 32 mm, friction <= 0.05 gives 50 mm");
  d8test::Check(SelectPipeSize(q, c, dia, both) == 3 && SelectPipeSize(q, c, dia, either) == 1, "pipe sizing: AND needs both (50 mm), OR takes the smaller (32 mm)");
  std::vector<double> reversed(dia.rbegin(), dia.rend());
  d8test::Check(SelectPipeSize(q, c, reversed, both) == 0 && SelectPipeSize(q, c, reversed, either) == 2, "pipe sizing: the choice does not depend on list order");
  PipeCriteria none;
  d8test::Check(SelectPipeSize(q, c, dia, none) == 0, "pipe sizing: with no criterion the smallest size qualifies");
  PipeCriteria tight;
  tight.use_friction = true;
  tight.max_gradient = 0.001;
  d8test::Check(SelectPipeSize(q, c, dia, tight) == -1 && SelectPipeSize(q, c, {}, vel) == -1, "pipe sizing: no size meets the limit -> -1");
}

// D8-REVIT-SYS-0079: "Report fill ratio of cable trays from cable data to check capacity."
// Ten 25 mm cables in a 300 x 100 mm tray: 10 * pi * 0.025^2 / 4 / 0.03 = 0.1636246. Mixed: four 20 mm and six 12 mm cables:
// (4 * pi * 0.0001 + 6 * pi * 0.000036)/0.03 = 0.0645074. Conduit of 21.2 mm inside diameter with three 6 mm cables: 3*(0.006)^2/(0.0212)^2 = 0.24031.
D8_EXTRA_TEST(Rw3CableTrayFillRatio) {
  d8test::Check(Near(TrayFillRatio(std::vector<double>(10, 0.025), 0.3, 0.1), 0.16362461737446843, 1e-12), "tray fill: ten 25 mm cables in 300 x 100 = 16.36 %");
  std::vector<double> mixed(4, 0.020);
  for (int i = 0; i < 6; ++i) mixed.push_back(0.012);
  d8test::Check(Near(TrayFillRatio(mixed, 0.3, 0.1), 0.06450736915371041, 1e-12), "tray fill: mixed cables add by area, 6.45 %");
  const double conduit = CableFillRatio({0.006, 0.006, 0.006}, 3.14159265358979323846 * 0.0212 * 0.0212 / 4.0);
  d8test::Check(Near(conduit, 3.0 * 0.006 * 0.006 / (0.0212 * 0.0212), 1e-12) && Near(conduit, 0.2403, 1e-4), "tray fill: round conduit fill from inside diameter");
  const double a = TrayFillRatio({0.02, 0.03}, 0.3, 0.1), b = TrayFillRatio({0.03, 0.02}, 0.3, 0.1);
  d8test::Check(Near(a, b, 1e-15) && TrayFillRatio({}, 0.3, 0.1) == 0.0 && TrayFillRatio({0.02}, 0.0, 0.1) == 0.0, "tray fill: order does not matter, empty tray 0, zero-size tray 0");
  d8test::Check(TrayFillRatio(std::vector<double>(10, 0.025), 0.3, 0.1) <= 0.4 && TrayFillRatio(std::vector<double>(30, 0.025), 0.3, 0.1) > 0.4, "tray fill: a limit of 0.4 supplied by the caller passes 10 cables and fails 30");
}

// D8-REVIT-SYS-0069: "Automatically choose circuit wire sizes from the circuit rating and wire run length so voltage drop stays under a
// limit (3 percent in Revit)."
// Copper 1.724e-8 ohm m, I = 20 A, L = 50 m, single phase 230 V (drop 2 I rho L / A): 1.5 mm2 -> 22.99 V (9.99 %), 2.5 -> 13.79 V (6.00 %),
// 4 -> 8.62 V (3.748 %), 6 -> 5.7467 V (2.4986 %), so 6 mm2 is the first under 3 %. Three phase 400 V (sqrt(3) I rho L / A): 2.5 mm2 ->
// 11.944 V (2.986 %) passes, 1.5 mm2 gives 19.9 V (4.98 %) fails -> 2.5 mm2.
D8_EXTRA_TEST(Rw3WireSizeByVoltageDrop) {
  const std::vector<double> areas = {1.5e-6, 2.5e-6, 4e-6, 6e-6, 10e-6};
  const WireChoice w1 = SizeWireByDrop(20, 50, 230, 1, 3.0, areas, 1.724e-8);
  d8test::Check(w1.ok && w1.index == 3 && Near(w1.drop_volts, 5.746666666666667, 1e-9) && Near(w1.drop_percent, 2.498550724637681, 1e-9), "wire size: single phase 20 A over 50 m needs 6 mm2 (2.4986 %)");
  size_t trial = areas.size();
  for (size_t i = 0; i < areas.size(); ++i)
    if (2.0 * 20.0 * 1.724e-8 / areas[i] * 50.0 / 230.0 * 100.0 <= 3.0) {
      trial = i;
      break;
    }
  d8test::Check(trial == w1.index, "wire size: trial over the table agrees");
  const WireChoice w3 = SizeWireByDrop(20, 50, 400, 3, 3.0, areas, 1.724e-8);
  d8test::Check(w3.ok && w3.index == 1 && Near(w3.drop_percent, 2.9860555922487446, 1e-9), "wire size: three phase 400 V needs 2.5 mm2 (2.986 %)");
  d8test::Check(!SizeWireByDrop(200, 500, 230, 1, 3.0, areas, 1.724e-8).ok, "wire size: no listed size is enough");
  d8test::Check(SizeWireByDrop(20, 50, 230, 1, 100.0, areas, 1.724e-8).index == 0, "wire size: a loose limit takes the smallest conductor");
  d8test::Check(SizeWireByDrop(20, 50, 230, 1, 2.0, areas, 1.724e-8).index == 4 && SizeWireByDrop(20, 50, 230, 1, 2.0, areas, 1.724e-8).ok, "wire size: a 2 % limit needs 10 mm2 (1.499 %)");
}

// D8-REVIT-SYS-0068: "Report total connected load and demand load for each panel, load classification and classification group, with
// demand factors applied." (the display in panel properties is not tested; the demand-factor kinds are the constant and the incremental
// tier form, from recollection of the product's demand-factor dialog, needs owner/independent check)
// Receptacles 4000 + 8000 = 12000 VA, tiers {first 5000 at 1.0, rest at 0.5} -> 5000 + 3500 = 8500; HVAC 6000 at constant 0.8 -> 4800;
// Lighting 2000 + 1500 = 3500 at 1.0; Other 1000 without a rule counts in full. Connected 22500, demand 17800.
// Three-phase 400 V line current of 16800 VA = 16800 / (sqrt(3) 400) = 24.2487 A.
D8_EXTRA_TEST(Rw3PanelConnectedDemandLoad) {
  DemandRule rec;
  rec.tiers = {{5000, 1.0}, {0, 0.5}};
  DemandRule hvac;
  hvac.constant = 0.8;
  DemandRule light;
  const std::map<std::string, DemandRule> rules = {{"Receptacle", rec}, {"HVAC", hvac}, {"Lighting", light}};
  const std::vector<LoadItem> items = {{"Receptacle", 4000}, {"HVAC", 6000}, {"Receptacle", 8000}, {"Lighting", 2000}, {"Lighting", 1500}, {"Other", 1000}};
  const PanelTotals t = PanelLoadTotals(items, rules);
  d8test::Check(Near(t.connected.at("Receptacle"), 12000) && Near(t.demand.at("Receptacle"), 8500) && Near(t.demand.at("HVAC"), 4800) && Near(t.demand.at("Lighting"), 3500) && Near(t.demand.at("Other"), 1000), "panel load: per-classification demand 8500 / 4800 / 3500 / 1000");
  d8test::Check(Near(t.total_connected, 22500) && Near(t.total_demand, 17800), "panel load: connected 22500 VA, demand 17800 VA");
  d8test::Check(Near(DemandLoad(3000, rec), 3000) && Near(DemandLoad(5000, rec), 5000) && Near(DemandLoad(5001, rec), 5000.5) && Near(DemandLoad(0, rec), 0), "panel load: tier boundary at 5000 VA");
  d8test::Check(Near(ThreePhaseCurrent(16800, 400), 24.24871130596428, 1e-9) && Near(ThreePhaseCurrent(16800, 400) * std::sqrt(3.0) * 400, 16800, 1e-6), "panel load: 16800 VA at 400 V is 24.2487 A");
  d8test::Check(PanelLoadTotals({}, rules).total_demand == 0.0, "panel load: an empty panel has no load");
}

// D8-ARCHICAD-MEP-0025: "Optionally split duct, pipe and cable carrier runs into individual pieces within manufacturer minimum and
// maximum lengths, stretching a too-short leftover piece to the minimum and shortening the previous one."
// min 2, max 3: L = 10 -> three 3s leave 1 (< 2): stretch it to 2 and shorten the previous piece by 1 -> {3, 3, 2, 2}; L = 9 -> {3,3,3};
// L = 7.5 -> {3, 2.5, 2}; L = 8.5 -> {3, 3, 2.5}; L = 2.5 -> one piece; L = 1.5 too short. min 2.9, max 3, L = 7 -> shortened piece 1.1 < 2.9: refused.
D8_EXTRA_TEST(Rw3RunSplitPieceLimits) {
  auto same = [](const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (std::fabs(a[i] - b[i]) > 1e-9) return false;
    return true;
  };
  d8test::Check(SplitRun(10, 2, 3).ok && same(SplitRun(10, 2, 3).pieces, {3, 3, 2, 2}), "run split: 10 m -> 3, 3, 2, 2");
  d8test::Check(same(SplitRun(9, 2, 3).pieces, {3, 3, 3}) && SplitRun(9, 2, 3).ok, "run split: exact multiple stays whole");
  d8test::Check(same(SplitRun(7.5, 2, 3).pieces, {3, 2.5, 2}) && same(SplitRun(8.5, 2, 3).pieces, {3, 3, 2.5}), "run split: 7.5 -> 3, 2.5, 2 and 8.5 -> 3, 3, 2.5");
  d8test::Check(SplitRun(2.5, 2, 3).ok && same(SplitRun(2.5, 2, 3).pieces, {2.5}) && !SplitRun(1.5, 2, 3).ok, "run split: a run within the limits is one piece, a shorter one is flagged");
  d8test::Check(!SplitRun(7, 2.9, 3).ok && !SplitRun(10, 3, 2).ok && !SplitRun(0, 2, 3).ok, "run split: infeasible limits are refused");
  bool all_valid = true;
  for (int i = 1; i <= 400; ++i) {
    const double len = 0.05 * i;
    const SplitResult r = SplitRun(len, 2, 3);
    double sum = 0;
    for (double p : r.pieces) sum += p;
    if (r.ok && !Near(sum, len, 1e-9)) all_valid = false;
    if (!r.ok && len >= 2 && !r.pieces.empty()) all_valid = false;
    if (r.ok)
      for (double p : r.pieces)
        if (p < 2 - 1e-9 || p > 3 + 1e-9) all_valid = false;
    // refusal is only allowed when one adjustment cannot work: k full pieces of 3 leave a rest in (0, 2) with 3 - (2 - rest) < 2
    const double rest = len - 3.0 * std::floor(len / 3.0 + 1e-12);
    const bool single_adjust_fails = len > 3 && rest > 1e-9 && rest < 2 && 3 - (2 - rest) < 2;
    if (!r.ok && len >= 2 && !single_adjust_fails) all_valid = false;
  }
  d8test::Check(all_valid, "run split: for every length 0.05..20 m the pieces sum to the run and respect the limits when accepted, and a refusal happens only where one adjustment cannot work");
}

// D8-REVIT-STRUCT-0092: "Load combinations can be generated from the chosen region and design code and added in bulk to the project table."
// ASCE 7-16 section 2.3.1 strength combinations (from recollection of the standard's text, needs owner/independent check): with D, L, Lr, S, R,
// W, E available there are 1 + 3 + 6 + 3 + 1 + 1 + 1 = 16 combinations (named below, typed from the standard, not produced by the code).
// With only D and L: 1.4D and 1.2D+1.6L. With D, L, S, W: six combinations (no E, no Lr, no R). EN 1990 expression 6.10 with permanent G1, G2 and
// variable Q1 (psi0 0.7), Q2 (0.5), Q3 (0.6): one combination per leading action, 1.35 G + 1.5 Q_lead + 1.5 psi0 Q_other.
D8_EXTRA_TEST(Rw3LoadCombinationGeneration) {
  const std::set<std::string> all = {"D", "L", "Lr", "S", "R", "W", "E"};
  const std::vector<LoadCombo> c = Asce7Lrfd(all);
  const std::set<std::string> expect = {"1.4D", "1.2D+1.6L+0.5Lr", "1.2D+1.6L+0.5S", "1.2D+1.6L+0.5R", "1.2D+1.6Lr+1L", "1.2D+1.6Lr+0.5W", "1.2D+1.6S+1L", "1.2D+1.6S+0.5W", "1.2D+1.6R+1L", "1.2D+1.6R+0.5W", "1.2D+1W+1L+0.5Lr", "1.2D+1W+1L+0.5S", "1.2D+1W+1L+0.5R", "1.2D+1E+1L+0.2S", "0.9D+1W", "0.9D+1E"};
  std::set<std::string> got;
  for (const LoadCombo& k : c) got.insert(k.name);
  d8test::Check(c.size() == 16 && got == expect, "load combinations: ASCE 7 LRFD with all seven cases gives the 16 listed combinations");
  const std::vector<LoadCombo> dl = Asce7Lrfd({"D", "L"});
  d8test::Check(dl.size() == 2 && dl[0].name == "1.4D" && dl[1].name == "1.2D+1.6L", "load combinations: dead and live only give 1.4D and 1.2D+1.6L");
  std::set<std::string> six;
  for (const LoadCombo& k : Asce7Lrfd({"D", "L", "S", "W"})) six.insert(k.name);
  d8test::Check(six == std::set<std::string>({"1.4D", "1.2D+1.6L+0.5S", "1.2D+1.6S+1L", "1.2D+1.6S+0.5W", "1.2D+1W+1L+0.5S", "0.9D+1W"}), "load combinations: D, L, S, W give the six applicable combinations");
  d8test::Check(Asce7Lrfd({"L"}).empty() && Asce7Lrfd({}).empty(), "load combinations: without dead load nothing is generated");
  const std::vector<LoadCombo> en = En1990_610({"G1", "G2"}, {{"Q1", 0.7}, {"Q2", 0.5}, {"Q3", 0.6}});
  d8test::Check(en.size() == 3 && en[0].name == "1.35G1+1.35G2+1.5Q1+0.75Q2+0.9Q3" && en[1].name == "1.35G1+1.35G2+1.05Q1+1.5Q2+0.9Q3" && en[2].name == "1.35G1+1.35G2+1.05Q1+0.75Q2+1.5Q3", "load combinations: EN 1990 6.10 gives one combination per leading action");
  d8test::Check(En1990_610({"G"}, {}).size() == 1 && En1990_610({"G"}, {})[0].name == "1.35G", "load combinations: permanent-only EN 1990 case");
}

// D8-REVIT-STRUCT-0027: "Define load natures, cases and combinations for analytical use."
// Case values D = 10, L = 5, Lr = 2, S = 3, R = 1, W = 4, E = -3 (kN): the 16 ASCE combinations evaluate (typed from hand arithmetic)
// to 14, 21, 21.5, 20.5, 20.2, 17.2, 21.8, 18.8, 18.6, 15.6, 22, 22.5, 21.5, 14.6, 13, 6 -> envelope maximum 22.5 (1.2D+1W+1L+0.5S),
// minimum 6 (0.9D+1E, with the negative seismic value). A case absent from the table contributes nothing.
D8_EXTRA_TEST(Rw3LoadCombinationEnvelope) {
  const std::map<std::string, double> v = {{"D", 10}, {"L", 5}, {"Lr", 2}, {"S", 3}, {"R", 1}, {"W", 4}, {"E", -3}};
  const std::map<std::string, double> hand = {{"1.4D", 14.0}, {"1.2D+1.6L+0.5Lr", 21.0}, {"1.2D+1.6L+0.5S", 21.5}, {"1.2D+1.6L+0.5R", 20.5}, {"1.2D+1.6Lr+1L", 20.2}, {"1.2D+1.6Lr+0.5W", 17.2}, {"1.2D+1.6S+1L", 21.8}, {"1.2D+1.6S+0.5W", 18.8}, {"1.2D+1.6R+1L", 18.6}, {"1.2D+1.6R+0.5W", 15.6}, {"1.2D+1W+1L+0.5Lr", 22.0}, {"1.2D+1W+1L+0.5S", 22.5}, {"1.2D+1W+1L+0.5R", 21.5}, {"1.2D+1E+1L+0.2S", 14.6}, {"0.9D+1W", 13.0}, {"0.9D+1E", 6.0}};
  const std::vector<LoadCombo> c = Asce7Lrfd({"D", "L", "Lr", "S", "R", "W", "E"});
  int matched = 0;
  double mx = -1e9, mn = 1e9;
  std::string mxn, mnn;
  for (const LoadCombo& k : c) {
    const double val = EvaluateCombo(k, v);
    auto it = hand.find(k.name);
    if (it != hand.end() && Near(it->second, val, 1e-9)) ++matched;
    if (val > mx) mx = val, mxn = k.name;
    if (val < mn) mn = val, mnn = k.name;
  }
  d8test::Check(matched == 16, "load combinations: all 16 combinations evaluate to the hand arithmetic values");
  d8test::Check(Near(mx, 22.5) && mxn == "1.2D+1W+1L+0.5S" && Near(mn, 6.0) && mnn == "0.9D+1E", "load combinations: envelope maximum 22.5 and minimum 6");
  LoadCombo one{"x", {{"D", 1.2}, {"Z", 99.0}}};
  d8test::Check(Near(EvaluateCombo(one, v), 12.0) && Near(EvaluateCombo(one, {}), 0.0), "load combinations: cases with no value contribute nothing");
  const std::vector<LoadCombo> en = En1990_610({"G"}, {{"Q1", 0.7}, {"Q2", 0.5}});
  const std::map<std::string, double> ev = {{"G", 10}, {"Q1", 4}, {"Q2", 6}};
  d8test::Check(Near(EvaluateCombo(en[0], ev), 1.35 * 10 + 1.5 * 4 + 1.5 * 0.5 * 6) && Near(EvaluateCombo(en[1], ev), 1.35 * 10 + 1.5 * 0.7 * 4 + 1.5 * 6), "load combinations: EN 1990 combinations evaluate by hand arithmetic");
}

// D8-REVIT-STRUCT-0035: "Apply line and area loads on members and panels with orientation and projected options."
// Trapezoidal line load 2 -> 6 kN/m over 3 m: total (2+6)/2 * 3 = 12, centroid 3 (2 + 12)/(3 * 8) = 1.75 from the start (also by a 3000-step
// midpoint integral of x w(x) / integral w); triangular 0 -> w: 2L/3; uniform: L/2. Area load 2 kN/m2 on a 4 x 5 plan with a 0.75 slope
// (surface factor 1.25): per surface area 2*20*1.25 = 50; per projected area 40; normal pressure on the surface: vertical 40, horizontal
// 30, resultant sqrt(40^2 + 30^2) = 50 = 2 * (25 m2 surface).
D8_EXTRA_TEST(Rw3LineAndAreaLoadResultants) {
  const LineLoadResultant t = TrapezoidLineLoad(2, 6, 3);
  double sw = 0, sxw = 0;
  const int n = 3000;
  for (int i = 0; i < n; ++i) {
    const double x = 3.0 * (i + 0.5) / n, w = 2.0 + (6.0 - 2.0) * x / 3.0;
    sw += w * 3.0 / n;
    sxw += x * w * 3.0 / n;
  }
  d8test::Check(Near(t.total, 12.0) && Near(t.centroid, 1.75) && Near(sw, t.total, 1e-6) && Near(sxw / sw, t.centroid, 1e-6), "line load: trapezoid 2 -> 6 over 3 m has 12 kN at 1.75 m (also by numeric integral)");
  d8test::Check(Near(TrapezoidLineLoad(0, 6, 3).centroid, 2.0) && Near(TrapezoidLineLoad(4, 4, 3).centroid, 1.5) && Near(TrapezoidLineLoad(6, 0, 3).centroid, 1.0) && Near(TrapezoidLineLoad(0, 0, 3).centroid, 1.5), "line load: triangular 2L/3, uniform L/2, reversed triangle L/3");
  const std::vector<P2> plan = {{0, 0}, {4, 0}, {4, 5}, {0, 5}};
  const AreaLoadForce a = AreaLoadOnPlane(plan, 0.75, 2.0, AreaLoadBasis::PerSurfaceArea), b = AreaLoadOnPlane(plan, 0.75, 2.0, AreaLoadBasis::PerProjectedArea), c = AreaLoadOnPlane(plan, 0.75, 2.0, AreaLoadBasis::NormalPerSurfaceArea);
  d8test::Check(Near(a.vertical, 50.0) && Near(a.horizontal, 0.0) && Near(b.vertical, 40.0) && Near(b.horizontal, 0.0), "area load: per surface area 50 kN, per projected area 40 kN");
  d8test::Check(Near(c.vertical, 40.0) && Near(c.horizontal, 30.0) && Near(std::hypot(c.vertical, c.horizontal), 2.0 * 25.0, 1e-9), "area load: normal pressure gives 40 vertical, 30 horizontal, resultant 2 x 25 = 50");
  d8test::Check(AreaLoadOnPlane(plan, 0.0, 2.0, AreaLoadBasis::PerSurfaceArea).vertical == 40.0 && AreaLoadOnPlane({}, 0.5, 2.0, AreaLoadBasis::PerProjectedArea).vertical == 0.0, "area load: a flat plane has equal bases; no plan, no load");
}

} // namespace
