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
#include <cstdint>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
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

// ---- CARBON-0004: the CSV export --------------------------------------------------------------------------------------------
// Whole-building carbon is routinely millions of kgCO2e: the export must keep every digit, never use an exponent, never group digits and
// use a point as the decimal separator whatever locale the host application set.
// Hand-written expected texts (the dyadic values add exactly: 1234.25 + 3456789 + 12345678.5 = 15803701.75). The shortest text that
// reads back to the same double: 1234.5678 -> "1234.5678", 0.1 + 0.2 -> "0.30000000000000004" (the well-known 17-digit case), 1e-7 ->
// "0.0000001", 1e21 -> "1000000000000000000000", 123456789012.345 -> "123456789012.345", negative zero -> "0".
// A field with a comma or a quote is quoted with doubled quotes (RFC 4180). The numbers survive a round trip bit for bit for 300
// pseudo-random values of widely different size (a linear congruential generator written here), read back by a classic-locale stream
// written here. The same output is produced when the global locale has a comma as decimal separator and a dot for digit grouping.
struct CommaPunct : std::numpunct<char> {
  char do_decimal_point() const override { return ','; }
  char do_thousands_sep() const override { return '.'; }
  std::string do_grouping() const override { return "\3"; }
};
struct GlobalLocaleGuard {
  std::locale saved;
  explicit GlobalLocaleGuard(const std::locale& l) : saved(std::locale::global(l)) {}
  ~GlobalLocaleGuard() { std::locale::global(saved); }
};
D8_EXTRA_TEST(Rw5FormaCarbonCsvExactDigitsAndLocale) {
  const std::string expected = "system,kgCO2e\n\"cores, stairs\",1234.25\nenvelope,3456789\nstructure,12345678.5\ntotal,15803701.75\n";
  const std::map<std::string, double> big = {{"structure", 12345678.5}, {"envelope", 3456789.0}, {"cores, stairs", 1234.25}};
  d8test::Check(CarbonCsv("system", big) == expected, "csv: values of 1e6 and more keep every digit, no exponent; the total 15803701.75; a field with a comma is quoted");
  d8test::Check(CarbonCsv("x", {{"a", 1234.5678}}) == "x,kgCO2e\na,1234.5678\ntotal,1234.5678\n", "csv: 1234.5678 is written in full, not rounded to 1234.57");
  d8test::Check(CarbonCsv("x", {{"a", 0.1 + 0.2}}) == "x,kgCO2e\na,0.30000000000000004\ntotal,0.30000000000000004\n", "csv: 0.1 + 0.2 needs its 17 digits to read back exactly");
  d8test::Check(CarbonCsv("x", {{"a", 1e-7}}) == "x,kgCO2e\na,0.0000001\ntotal,0.0000001\n" && CarbonCsv("x", {{"a", 1e21}}) == "x,kgCO2e\na,1000000000000000000000\ntotal,1000000000000000000000\n" &&
                    CarbonCsv("x", {{"a", 123456789012.345}}) == "x,kgCO2e\na,123456789012.345\ntotal,123456789012.345\n" && CarbonCsv("x", {{"a", -0.0}}) == "x,kgCO2e\na,0\ntotal,0\n",
                "csv: 1e-7, 1e21, 123456789012.345 and negative zero are plain decimals");
  d8test::Check(CarbonCsv("a \"b\", c", {{"say \"hi\"", 1}}) == "\"a \"\"b\"\", c\",kgCO2e\n\"say \"\"hi\"\"\",1\ntotal,1\n", "csv: quotes inside a field are doubled and the field is quoted (title and group name)");
  // round trip of 300 values (a = 6364136223846793005 a + 1442695040888963407 mod 2^64; value = (top 52 bits as a fraction) times 10^e, e from -5 to 9)
  std::uint64_t st = 12345;
  std::map<std::string, double> many;
  std::vector<double> sent;
  for (int i = 0; i < 300; ++i) {
    st = st * 6364136223846793005ULL + 1442695040888963407ULL;
    const double frac = static_cast<double>(st >> 12) / 4503599627370496.0;   // 52 bits
    const int e = static_cast<int>((st >> 4) % 15) - 5;
    const double v = (0.1 + frac) * std::pow(10.0, e);
    char key[16];
    std::snprintf(key, sizeof key, "g%03d", i);
    many[key] = v;
    sent.push_back(v);
  }
  auto roundTrips = [&](const std::string& csv) {
    std::istringstream in(csv);
    in.imbue(std::locale::classic());
    std::string line;
    std::getline(in, line);   // header
    int n = 0;
    for (; n < 300 && std::getline(in, line); ++n) {
      const std::string num = line.substr(line.find(',') + 1);
      if (num.find_first_of("eE.,") != std::string::npos && num.find('.') == std::string::npos) return false;   // no exponent, no comma
      if (num.find_first_of("eE,") != std::string::npos) return false;
      std::istringstream ns(num);
      ns.imbue(std::locale::classic());
      double back = 0;
      ns >> back;
      if (ns.fail() || !(back == sent[n])) return false;   // bit for bit: keys g000 .. g299 sort in the order they were made
    }
    return n == 300;
  };
  const std::string classicText = CarbonCsv("g", many);
  d8test::Check(roundTrips(classicText), "csv: 300 pseudo-random values between 1e-5 and 1e9 read back bit for bit, with no exponent and no comma in a number");
  {
    GlobalLocaleGuard g(std::locale(std::locale::classic(), new CommaPunct));
    std::ostringstream probe;   // the host locale really is a comma one now (with the default global locale)
    probe << std::fixed << std::setprecision(1) << 1234567.5;
    d8test::Check(probe.str() == "1.234.567,5", "csv locale: the test's global locale writes a fixed 1234567.5 as 1.234.567,5 (so an unimbued stream would break the export)");
    d8test::Check(CarbonCsv("system", big) == expected && CarbonCsv("g", many) == classicText, "csv locale: with a comma-decimal, dot-grouping global locale the export is byte for byte the same");
  }
  // decoy: what the previous default-stream code wrote for these values
  std::ostringstream oldStyle;
  oldStyle << 3456789.0 << " " << 1234.5678;
  d8test::Check(oldStyle.str() == "3.45679e+06 1234.57", "csv decoy: a default stream gives 3.45679e+06 and 1234.57, which is not what the export may write");
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

// ---- CARBON-0008: regions with local factors, climate and grid data, added over time ---------------------------------------
// Authored packs (not from any database). Items: 10 m3 concrete and 1000 kg rebar; 50000 kWh a year for 10 years from 2026.
//  UK: concrete A1A3 250 + A4 20; rebar A1A3 1.2 + A4 0.05; grid (2025, 0.20) (2035, 0.05); temperatures 100 days at 0, 100 at 10, 100 at 20,
//      65 at 25 C.  embodied 10 x 270 + 1000 x 1.25 = 2700 + 1250 = 3950; operational 50000 x (0.185 + ... + 0.05) = 50000 x 1.175 = 58750;
//      total 62700; heating degree days at 15.5 C: 100 x 15.5 + 100 x 5.5 = 2100; cooling at 22 C: 65 x 3 = 195.
//  DE: concrete A1A3 220 + A4 25 (no rebar); grid (2025, 0.35) (2040, 0.10); temperatures 80 days at -2, 120 at 8, 100 at 18, 65 at 28 C.
//      embodied 10 x 245 = 2450 with rebar missing; operational factors 0.35 - 0.25/15 k, k = 1..10: sum 3.5 - 0.25 x 55 / 15 = 2.58333, so
//      50000 x 2.58333 = 129166.67; total 131616.67; HDD at 15.5: 80 x 17.5 + 120 x 7.5 = 2300; CDD at 22: 65 x 6 = 390.
//  NO (added later): concrete A1A3 180 + A4 30; grid (2025, 0.03) flat; 365 days at 4 C. embodied 2100; operational 50000 x 10 x 0.03 =
//      15000; total 17100; HDD at 15.5: 365 x 11.5 = 4197.5; CDD at 22: 0.
// The results of UK and DE do not change when NO is added. Invalid packs are refused with exact reasons and leave the library as it was.
RegionPack MakePack(const char* code, std::vector<EmissionFactor> f, std::vector<std::pair<int, double>> grid, std::vector<std::pair<int, double>> days) {
  RegionPack p;
  p.code = code;
  p.embodied = f;
  p.gridPerYear = grid;
  for (const auto& d : days)
    for (int i = 0; i < d.first; ++i) p.dailyMeanTempC.push_back(d.second);
  return p;
}
D8_EXTRA_TEST(Rw5FormaRegionPacks) {
  RegionLibrary lib;
  const std::vector<CarbonItem> items = {{"e1", "structure", "concrete", 10}, {"e2", "structure", "rebar", 1000}};
  const RegionPack uk = MakePack("UK", {Fac("concrete", "x", "m3", {{"A1A3", 250}, {"A4", 20}}), Fac("rebar", "x", "kg", {{"A1A3", 1.2}, {"A4", 0.05}})}, {{2035, 0.05}, {2025, 0.20}}, {{100, 0}, {100, 10}, {100, 20}, {65, 25}});
  const RegionPack de = MakePack("DE", {Fac("concrete", "x", "m3", {{"A1A3", 220}, {"A4", 25}})}, {{2025, 0.35}, {2040, 0.10}}, {{80, -2}, {120, 8}, {100, 18}, {65, 28}});
  d8test::Check(lib.Add(uk).empty() && lib.Add(de).empty() && lib.Regions().size() == 2 && lib.Regions()[0] == "UK" && lib.Regions()[1] == "DE", "regions: UK and DE packs are accepted, in the order added");
  const RegionalCarbonResult ruk = RegionalCarbon(lib, "UK", items, 50000, 2026, 10), rde = RegionalCarbon(lib, "DE", items, 50000, 2026, 10);
  d8test::Check(ruk.ok && Near(ruk.embodied, 3950, 1e-9) && Near(ruk.operational, 58750, 1e-6) && Near(ruk.total, 62700, 1e-6) && ruk.missing.empty(), "regions: UK local factors and grid table give 3950 + 58750 = 62700 kgCO2e");
  d8test::Check(rde.ok && Near(rde.embodied, 2450, 1e-9) && Near(rde.operational, 129166.6666666667, 1e-5) && Near(rde.total, 131616.6666666667, 1e-5) && rde.missing.size() == 1 && rde.missing[0] == "rebar",
                "regions: DE has its own factors (concrete 245, rebar reported missing) and grid table: 2450 + 129166.67 = 131616.67 kgCO2e");
  d8test::Check(Near(lib.DegreeDaysOf("UK", 15.5, true), 2100, 1e-9) && Near(lib.DegreeDaysOf("UK", 22, false), 195, 1e-9) && Near(lib.DegreeDaysOf("DE", 15.5, true), 2300, 1e-9) && Near(lib.DegreeDaysOf("DE", 22, false), 390, 1e-9) &&
                    lib.DegreeDaysOf("XX", 15.5, true) == 0,
                "regions: climate gives heating degree days 2100 (UK) and 2300 (DE) at 15.5 C and cooling degree days 195 and 390 at 22 C; an unknown region has none");
  // the region of the factor decides: the UK rebar factor is not found in DE, and the climate of one region is not the other's
  d8test::Check(lib.Factors().Find("rebar", "UK") != nullptr && lib.Factors().Find("rebar", "DE") == nullptr && lib.Factors().Find("concrete", "DE")->perUnit.at("A1A3") == 220 && lib.Factors().Find("concrete", "UK")->region == "UK",
                "regions: factors are kept per region (the UK rebar is not visible in DE, the pack's own region code is stored)");
  // adding a region later changes nothing for the others
  const RegionPack no = MakePack("NO", {Fac("concrete", "x", "m3", {{"A1A3", 180}, {"A4", 30}})}, {{2025, 0.03}}, {{365, 4}});
  d8test::Check(lib.Add(no).empty() && lib.Regions().size() == 3, "regions: NO is added later");
  const RegionalCarbonResult rno = RegionalCarbon(lib, "NO", items, 50000, 2026, 10), uk2 = RegionalCarbon(lib, "UK", items, 50000, 2026, 10), de2 = RegionalCarbon(lib, "DE", items, 50000, 2026, 10);
  d8test::Check(rno.ok && Near(rno.embodied, 2100, 1e-9) && Near(rno.operational, 15000, 1e-9) && Near(rno.total, 17100, 1e-9) && Near(lib.DegreeDaysOf("NO", 15.5, true), 4197.5, 1e-9) && lib.DegreeDaysOf("NO", 22, false) == 0,
                "regions: NO gives 2100 + 15000 = 17100 kgCO2e, 4197.5 heating degree days and no cooling");
  d8test::Check(uk2.total == ruk.total && uk2.embodied == ruk.embodied && uk2.operational == ruk.operational && de2.total == rde.total && de2.operational == rde.operational && lib.DegreeDaysOf("UK", 15.5, true) == 2100,
                "regions: the UK and DE results are bit for bit the same after NO was added");
  d8test::Check(!RegionalCarbon(lib, "FR", items, 50000, 2026, 10).ok, "regions: an unknown region gives no result");
  // schema errors, exact
  RegionPack bad;
  bad.dailyMeanTempC.assign(364, 10.0);
  const std::vector<std::string> e1 = lib.Add(bad);
  d8test::Check(e1.size() == 4 && e1[0] == "code: empty" && e1[1] == "embodied: no factors" && e1[2] == "grid: no table" && e1[3] == "climate: 364 daily values, need 365 or 366", "regions schema: an empty pack is refused with its four reasons, in order");
  RegionPack bad2 = MakePack("FR", {Fac("", "x", "", {})}, {{2030, 0.1}, {2030, -0.1}}, {{365, 10}});
  bad2.dailyMeanTempC[2] = 70.0;
  const std::vector<std::string> e2 = lib.Add(bad2);
  d8test::Check(e2.size() == 6 && e2[0] == "embodied[0]: empty material" && e2[1] == "embodied[0]: empty unit" && e2[2] == "embodied[0]: no stages" && e2[3] == "grid: year 2030 twice" &&
                    e2[4] == "grid: year 2030 has a negative or non-finite factor" && e2[5] == "climate: day 3 outside -90 .. 60 C",
                "regions schema: an empty factor row, a repeated year with a negative factor and a 70 C day are refused with six exact reasons");
  const std::vector<std::string> e3 = lib.Add(uk);
  d8test::Check(e3.size() == 1 && e3[0] == "code: region UK already present" && lib.Regions().size() == 3 && !lib.Has("FR"), "regions schema: adding UK a second time is refused and nothing changes");
  RegionPack ok366 = MakePack("SE", {Fac("concrete", "x", "m3", {{"A1A3", 200}})}, {{2025, 0.04}}, {{366, 3}});
  d8test::Check(lib.Add(ok366).empty() && Near(lib.DegreeDaysOf("SE", 15.5, true), 366 * 12.5, 1e-9), "regions schema: a leap-year climate of 366 days is accepted (366 x 12.5 = 4575 heating degree days)");
  d8test::Check(!Near(rde.operational, 50000 * 10 * 0.35, 1.0) && !Near(rde.embodied, ruk.embodied, 1.0), "regions decoy: the DE result is not the UK embodied value and not a flat 2025 grid factor");
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
