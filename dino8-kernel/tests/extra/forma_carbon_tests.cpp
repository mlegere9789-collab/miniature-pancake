// Tests of include/dino8/kernel/forma_carbon.h: embodied carbon as quantity x factor by stage, factor database with regions and user
// entries, grid factors per region and year, whole-life totals per area, dashboard breakdowns with hot spots, scenarios over time against
// a target (Register rows D8-FORMA-CARBON-0001 .. 0005, 0008 and D8-FORMA-ANALYSIS-0027). The emission factors below are AUTHORED for
// the tests (they are not taken from any database); every total is worked out by hand in the comments.
//
// Factors (kgCO2e per unit), stages A1A3, A4, A5, C1C4, D:
//   concrete (m3):   300, 20, 12, 18, -
//   rebar (kg):      1.4, 0.04, 0.03, 0.02, -0.5
//   timber (m3):     120, 30, 10, 250, -150
//   glazing (m2):    85, 5, 2, 6, -
// Items: e1 slab (structure) 420 m3 concrete; e2 rebar (structure) 33600 kg; e3 walls (envelope) 150 m3 timber; e4 windows (envelope)
// 800 m2 glazing.
//   A1A3: 126000 + 47040 + 18000 + 68000 = 259040     A4: 8400 + 1344 + 4500 + 4000 = 18244     A5: 5040 + 1008 + 1500 + 1600 = 9148
//   C1C4: 7560 + 672 + 37500 + 4800 = 50532            D:  -16800 - 22500 = -39300
//   total 259040 + 18244 + 9148 + 50532 - 39300 = 297664
//   by material: concrete 147000, rebar 47040 + 1344 + 1008 + 672 - 16800 = 33264, timber 18000 + 4500 + 1500 + 37500 - 22500 = 39000,
//   glazing 78400 (sum 297664); by system: structure 180264, envelope 117400; by element: e1 147000, e2 33264, e3 39000, e4 78400.
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "dino8/kernel/forma_carbon.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::forma;

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

EmissionFactor Fac(const char* mat, const char* region, const char* unit, std::map<std::string, double> st, bool user = false) {
  EmissionFactor f;
  f.material = mat;
  f.region = region;
  f.unit = unit;
  f.perUnit = st;
  f.user = user;
  return f;
}
FactorDb AuthoredDb() {
  FactorDb db;
  db.Add(Fac("concrete", "global", "m3", {{"A1A3", 300}, {"A4", 20}, {"A5", 12}, {"C1C4", 18}}));
  db.Add(Fac("rebar", "global", "kg", {{"A1A3", 1.4}, {"A4", 0.04}, {"A5", 0.03}, {"C1C4", 0.02}, {"D", -0.5}}));
  db.Add(Fac("timber", "global", "m3", {{"A1A3", 120}, {"A4", 30}, {"A5", 10}, {"C1C4", 250}, {"D", -150}}));
  db.Add(Fac("glazing", "global", "m2", {{"A1A3", 85}, {"A4", 5}, {"A5", 2}, {"C1C4", 6}}));
  return db;
}
std::vector<CarbonItem> AuthoredItems() {
  return {{"e1", "structure", "concrete", 420}, {"e2", "structure", "rebar", 33600}, {"e3", "envelope", "timber", 150}, {"e4", "envelope", "glazing", 800}};
}
double SumOf(const std::map<std::string, double>& m) {
  double s = 0;
  for (const auto& kv : m) s += kv.second;
  return s;
}

// ---- CARBON-0002 -----------------------------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw5FormaEmbodiedCarbonByStage) {
  const FactorDb db = AuthoredDb();
  const CarbonBreakdown r = EmbodiedCarbon(db, "global", AuthoredItems());
  d8test::Check(Near(r.byStage.at("A1A3"), 259040, 1e-6) && Near(r.byStage.at("A4"), 18244, 1e-6) && Near(r.byStage.at("A5"), 9148, 1e-6) && Near(r.byStage.at("C1C4"), 50532, 1e-6) && Near(r.byStage.at("D"), -39300, 1e-6),
                "embodied carbon: stages A1A3 259040, A4 18244, A5 9148, C1C4 50532, D -39300 (hand sums of quantity x factor)");
  d8test::Check(Near(r.total, 297664, 1e-6) && Near(r.total, 259040 + 18244 + 9148 + 50532 - 39300, 1e-6), "embodied carbon: the total is the sum of the stages, 297664 kgCO2e");
  d8test::Check(r.missing.empty() && r.byStage.size() == 5, "embodied carbon: five stages, nothing missing");
  const CarbonBreakdown a = EmbodiedCarbon(db, "global", AuthoredItems(), {"A1A3"});
  d8test::Check(Near(a.total, 259040, 1e-6) && a.byStage.size() == 1, "embodied carbon: limiting to A1A3 gives 259040");
  const CarbonBreakdown a15 = EmbodiedCarbon(db, "global", AuthoredItems(), {"A1A3", "A4", "A5"});
  d8test::Check(Near(a15.total, 259040 + 18244 + 9148, 1e-6) && Near(a15.total, 286432, 1e-6), "embodied carbon: A1 to A5 is 286432");
  std::vector<CarbonItem> items = AuthoredItems();
  items.push_back({"e5", "envelope", "mycelium", 10});
  const CarbonBreakdown m = EmbodiedCarbon(db, "global", items);
  d8test::Check(Near(m.total, 297664, 1e-6) && m.missing.size() == 1 && m.missing[0] == "mycelium", "embodied carbon: a material without a factor is reported and adds nothing");
  d8test::Check(Near(EmbodiedCarbon(db, "global", {{"x", "s", "concrete", 2.0 * 420}}).total, 2 * 147000, 1e-6), "embodied carbon: doubling the quantity doubles the carbon (linear, 294000 for 840 m3 of concrete)");
  d8test::Check(!Near(r.total, 259040 + 18244 + 9148 + 50532, 1.0), "embodied carbon decoy: dropping the module D credit would give 336964, not 297664");
}

// ---- CARBON-0004 -----------------------------------------------------------------------------------------------------------
D8_EXTRA_TEST(Rw5FormaCarbonBreakdownHotSpots) {
  const CarbonBreakdown r = EmbodiedCarbon(AuthoredDb(), "global", AuthoredItems());
  d8test::Check(Near(r.byMaterial.at("concrete"), 147000, 1e-6) && Near(r.byMaterial.at("rebar"), 33264, 1e-6) && Near(r.byMaterial.at("timber"), 39000, 1e-6) && Near(r.byMaterial.at("glazing"), 78400, 1e-6),
                "breakdown: by material concrete 147000, rebar 33264, timber 39000, glazing 78400");
  d8test::Check(Near(r.bySystem.at("structure"), 180264, 1e-6) && Near(r.bySystem.at("envelope"), 117400, 1e-6) && Near(r.byElement.at("e1"), 147000, 1e-6) && Near(r.byElement.at("e2"), 33264, 1e-6) &&
                    Near(r.byElement.at("e3"), 39000, 1e-6) && Near(r.byElement.at("e4"), 78400, 1e-6),
                "breakdown: by system structure 180264, envelope 117400; by element 147000, 33264, 39000, 78400");
  d8test::Check(Near(SumOf(r.byMaterial), r.total, 1e-9 * 297664) && Near(SumOf(r.bySystem), r.total, 1e-9 * 297664) && Near(SumOf(r.byElement), r.total, 1e-9 * 297664) && Near(SumOf(r.byStage), r.total, 1e-9 * 297664),
                "breakdown: material, system, element and stage groupings each add to the total within 1e-9 relative");
  // hot spots: share of 297664: concrete 49.39 %, glazing 26.34 %, timber 13.10 %, rebar 11.18 %
  const auto h30 = HotSpots(r.byMaterial, 0.30);
  d8test::Check(h30.size() == 1 && h30[0].first == "concrete", "hot spots: only concrete reaches a 30 % share");
  const auto h25 = HotSpots(r.byMaterial, 0.25);
  d8test::Check(h25.size() == 2 && h25[0].first == "concrete" && h25[1].first == "glazing" && Near(h25[1].second, 78400, 1e-9), "hot spots: at 25 % concrete then glazing, largest first");
  const auto h10 = HotSpots(r.byMaterial, 0.10);
  d8test::Check(h10.size() == 4 && h10[2].first == "timber" && h10[3].first == "rebar", "hot spots: at 10 % all four, ordered 147000, 78400, 39000, 33264");
  d8test::Check(HotSpots(r.byMaterial, 0.60).empty(), "hot spots: nothing reaches 60 %");
  const auto tie = HotSpots({{"a", 25.0}, {"b", 25.0}, {"c", 50.0}}, 0.25);
  d8test::Check(tie.size() == 3 && tie[0].first == "c" && tie[1].first == "a" && tie[2].first == "b", "hot spots: a group exactly at the threshold share counts (25 of 100), ties ordered by name");
  const auto el = HotSpots(r.byElement, 0.30);
  d8test::Check(el.size() == 1 && el[0].first == "e1", "hot spots: by element only the slab e1 passes 30 %");
  const std::string csv = CarbonCsv("system", r.bySystem);
  d8test::Check(csv == "system,kgCO2e\nenvelope,117400\nstructure,180264\ntotal,297664\n", "dashboard export: the system breakdown as a CSV table with its total row");
  d8test::Check(!Near(r.byMaterial.at("concrete") / r.total, 0.25, 0.1), "breakdown decoy: concrete share is 49.4 %, far from an equal split (25 %)");
}

// ---- CARBON-0003 -----------------------------------------------------------------------------------------------------------
// Lookup order: user entry for the region, user entry for "global", built-in for the region, built-in for "global". Concrete: built-in global
// A1A3 300, built-in DE 260. Region FR (no entry of its own): 300. DE: 260. A user "global" concrete of 280: FR 280, DE 280 (user entries first).
// A user DE entry of 240: DE 240, FR still 280. Unknown material: none; after the user adds it, found with its unit.
D8_EXTRA_TEST(Rw5FormaFactorDatabaseUserEntries) {
  FactorDb db = AuthoredDb();
  db.Add(Fac("concrete", "DE", "m3", {{"A1A3", 260}}));
  auto a1 = [&](const char* mat, const char* region) {
    const EmissionFactor* f = db.Find(mat, region);
    return f ? f->perUnit.at("A1A3") : -1.0;
  };
  d8test::Check(a1("concrete", "FR") == 300 && a1("concrete", "DE") == 260 && a1("concrete", "global") == 300, "factor database: FR falls back to global 300, DE has its own 260");
  d8test::Check(db.Find("mycelium", "DE") == nullptr, "factor database: an unknown material has no factor");
  db.Add(Fac("concrete", "global", "m3", {{"A1A3", 280}}, true));
  d8test::Check(a1("concrete", "FR") == 280 && a1("concrete", "DE") == 280, "factor database: a user global entry (280) comes before the built-in ones, also before the built-in DE entry");
  db.Add(Fac("concrete", "DE", "m3", {{"A1A3", 240}}, true));
  d8test::Check(a1("concrete", "DE") == 240 && a1("concrete", "FR") == 280, "factor database: a user DE entry (240) wins in DE and leaves FR at 280");
  db.Add(Fac("mycelium", "global", "m3", {{"A1A3", -40}}, true));
  const EmissionFactor* my = db.Find("mycelium", "UK");
  d8test::Check(my && my->unit == "m3" && my->user && Near(my->perUnit.at("A1A3"), -40, 0), "factor database: a user-added material is found in any region with its unit");
  // the result follows the lookup: 10 m3 of concrete in DE = 2400 A1A3 now, FR 2800, and with 10 m3 mycelium -400
  const double de = EmbodiedCarbon(db, "DE", {{"x", "s", "concrete", 10}}, {"A1A3"}).total, fr = EmbodiedCarbon(db, "FR", {{"x", "s", "concrete", 10}}, {"A1A3"}).total;
  d8test::Check(Near(de, 2400, 1e-9) && Near(fr, 2800, 1e-9) && Near(EmbodiedCarbon(db, "DE", {{"y", "s", "mycelium", 10}}).total, -400, 1e-9), "factor database: 10 m3 of concrete is 2400 kgCO2e in DE and 2800 in FR, 10 m3 of mycelium -400");
  d8test::Check(db.Regions().count("DE") == 1 && db.Regions().count("global") == 1 && db.Regions().size() == 2, "factor database: the regions present are global and DE");
}

// ---- CARBON-0008 and ANALYSIS-0027 -----------------------------------------------------------------------------------------
// Grid factors (kgCO2e per kWh), authored. UK: (2025, 0.20), (2035, 0.05): linear, 0.015 per year: 2030 -> 0.125, 2026 -> 0.185; held at
// 0.20 before 2025 and 0.05 after 2035. PL: (2025, 0.70), (2040, 0.35): 2030 -> 0.70 - 5/15 x 0.35 = 0.58333. 100000 kWh per year for 10
// years from 2026: the factors 0.185, 0.17, ... 0.05 are an arithmetic series with sum 10 x (0.185 + 0.05) / 2 = 1.175, so 117500 kgCO2e. For
// 12 years two more years at 0.05 add 10000: 127500. PL for 10 years from 2026: factors 0.70 - 0.35/15 x k, k = 1..10: sum 7.0 - (0.35/15) x 55 = 5.71667 -> 571666.67.
// Adding a region later (NO) leaves the other regions' results unchanged; an unknown region has no table and gives 0.
D8_EXTRA_TEST(Rw5FormaGridFactorsPerRegionYear) {
  GridFactors g;
  g.SetRegion("UK", {{2035, 0.05}, {2025, 0.20}});   // unsorted on purpose
  g.SetRegion("PL", {{2025, 0.70}, {2040, 0.35}});
  d8test::Check(Near(g.Factor("UK", 2030), 0.125, 1e-12) && Near(g.Factor("UK", 2026), 0.185, 1e-12) && Near(g.Factor("UK", 2020), 0.20, 1e-12) && Near(g.Factor("UK", 2050), 0.05, 1e-12),
                "grid factors: UK 0.125 in 2030, 0.185 in 2026, held at 0.20 before 2025 and at 0.05 after 2035");
  d8test::Check(Near(g.Factor("PL", 2030), 0.70 - 5.0 / 15.0 * 0.35, 1e-12) && Near(g.Factor("PL", 2040), 0.35, 1e-12) && Near(g.Factor("PL", 2025), 0.70, 1e-12), "grid factors: PL 0.58333 in 2030");
  const double uk10 = OperationalCarbon(g, "UK", 100000, 2026, 10), uk12 = OperationalCarbon(g, "UK", 100000, 2026, 12), pl10 = OperationalCarbon(g, "PL", 100000, 2026, 10);
  d8test::Check(Near(uk10, 117500, 1e-6) && Near(uk12, 127500, 1e-6) && Near(pl10, 571666.6666666667, 1e-6), "operational carbon: 100000 kWh/year for 10 years in the UK is 117500 kgCO2e, 12 years 127500, PL 10 years 571667");
  d8test::Check(!Near(uk10, 100000 * 10 * 0.20, 1000.0), "operational carbon decoy: a flat 2025 factor would give 200000, not 117500");
  d8test::Check(g.Has("UK") && !g.Has("NO") && Near(g.Factor("NO", 2030), 0, 0), "grid factors: an unknown region has no table and factor 0");
  g.SetRegion("NO", {{2025, 0.03}});
  d8test::Check(g.Has("NO") && Near(g.Factor("NO", 2031), 0.03, 1e-12) && Near(OperationalCarbon(g, "UK", 100000, 2026, 10), uk10, 0) && Near(OperationalCarbon(g, "PL", 100000, 2026, 10), pl10, 0),
                "grid factors: adding a region later leaves the UK and PL results unchanged");
}

// ---- CARBON-0001 -----------------------------------------------------------------------------------------------------------
// Whole-life: embodied 297664 (the items above) + operational 117500 (UK, 10 years) = 415164 kgCO2e; floor area 2500 m2: 119.0656, 47.0
// and 166.0656 kgCO2e per m2. A zero floor area gives zero per-area values instead of dividing.
D8_EXTRA_TEST(Rw5FormaWholeLifeCarbonTotals) {
  GridFactors g;
  g.SetRegion("UK", {{2025, 0.20}, {2035, 0.05}});
  const CarbonBreakdown e = EmbodiedCarbon(AuthoredDb(), "global", AuthoredItems());
  const double op = OperationalCarbon(g, "UK", 100000, 2026, 10);
  const WholeLife w = WholeLifeCarbon(e.total, op, 2500);
  d8test::Check(Near(w.embodied, 297664, 1e-6) && Near(w.operational, 117500, 1e-6) && Near(w.total, 415164, 1e-6) && Near(w.total, w.embodied + w.operational, 1e-9), "whole-life carbon: 297664 + 117500 = 415164 kgCO2e");
  d8test::Check(Near(w.embodiedPerM2, 119.0656, 1e-9) && Near(w.operationalPerM2, 47.0, 1e-12) && Near(w.totalPerM2, 166.0656, 1e-9) && Near(w.totalPerM2, w.embodiedPerM2 + w.operationalPerM2, 1e-9), "whole-life carbon: 119.0656 + 47.0 = 166.0656 kgCO2e per m2");
  const WholeLife z = WholeLifeCarbon(1000, 500, 0);
  d8test::Check(Near(z.total, 1500, 0) && z.totalPerM2 == 0, "whole-life carbon: without floor area the per-area values are zero");
  d8test::Check(!Near(w.totalPerM2, w.total / 25000.0, 1.0), "whole-life carbon decoy: a wrong area (25000 m2) changes the intensity from 166 to 16.6");
}

// ---- CARBON-0005 -----------------------------------------------------------------------------------------------------------
// Scenarios: A embodied 200000, operational 20000 per year; B embodied 400000, 10000 per year. Cumulative after t years: A 200000 + 20000 t,
// B 400000 + 10000 t. They meet at t = 20 (both 600000); A is lower before (t = 10: 400000 against 500000) and higher after (t = 30: 800000
// against 700000). Crossover year 20. Target 700000 over 30 years: A is 100000 over (does not meet), B is exactly on it (meets, gap 0).
// A scenario with a falling operational series (20000, 19000, ... 1000) reaches 200000 + 210000 = 410000 after 20 years (sum 20 x 21 / 2 x 1000
// = 210000).
D8_EXTRA_TEST(Rw5FormaScenarioComparison) {
  CarbonScenario a, b, c;
  a.name = "A"; a.embodied = 200000; a.operationalPerYear.assign(60, 20000);
  b.name = "B"; b.embodied = 400000; b.operationalPerYear.assign(60, 10000);
  c.name = "C"; c.embodied = 200000;
  for (int y = 0; y < 20; ++y) c.operationalPerYear.push_back(20000 - 1000 * y);
  d8test::Check(Near(a.Cumulative(10), 400000, 1e-9) && Near(b.Cumulative(10), 500000, 1e-9) && Near(a.Cumulative(20), 600000, 1e-9) && Near(b.Cumulative(20), 600000, 1e-9) && Near(a.Cumulative(30), 800000, 1e-9) &&
                    Near(b.Cumulative(30), 700000, 1e-9),
                "scenarios: A and B cumulative 400000 / 500000 at 10 years, both 600000 at 20, 800000 / 700000 at 30");
  d8test::Check(CrossoverYear(a, b, 60) == 20 && CrossoverYear(b, a, 60) == 0 && CrossoverYear(a, b, 15) == -1, "scenarios: A reaches B at year 20; B is already above A at year 0; no crossover inside 15 years");
  const TargetGap ga = CompareToTarget(a.Cumulative(30), 700000), gb = CompareToTarget(b.Cumulative(30), 700000);
  d8test::Check(!ga.meets && Near(ga.gap, 100000, 1e-9) && gb.meets && Near(gb.gap, 0, 1e-9), "scenarios: against a 700000 target A is 100000 over and B is exactly on it");
  d8test::Check(Near(c.Cumulative(20), 410000, 1e-9) && Near(c.Cumulative(40), 410000, 1e-9), "scenarios: a falling operational series gives 200000 + 210000 = 410000 and stops growing after its last year");
  d8test::Check(!Near(a.Cumulative(30), a.Cumulative(20), 1.0), "scenarios decoy: carbon keeps accumulating with the years (600000 to 800000)");
}

}  // namespace
