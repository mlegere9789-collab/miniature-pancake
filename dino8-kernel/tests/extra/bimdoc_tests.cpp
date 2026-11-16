// Documentation and collaboration logic of include/dino8/kernel/bimdoc_calc.h: schedule engine (calculated, percentage and combined
// fields, conditions, criteria, sorting, grouping, totals, merging, zero text, delimited export), material takeoff, mark numbering,
// revisions, shared-model ownership and model comparison. One test per Register row. Every expected value is worked out by hand in the
// comment of the test (decimal literals computed outside the code under test). The register rows' acceptance sketches (taken from
// the vendor pages they cite) supply the scenarios; the vendor pages themselves could not be fetched from this environment, so the
// behaviours the rows do not state are listed in the header of bimdoc_calc.h and in the row notes. Selection, undo, UI, cloud
// service and file-format clauses are not tested.
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/bimdoc_calc.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::bimdoc;

bool Near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

Field P(const std::string& n, int decimals = -1) { Field f; f.name = n; f.decimals = decimals; return f; }

Record Rec(std::initializer_list<std::pair<const char*, Value>> kv) {
  Record r;
  for (auto& p : kv) r[p.first] = p.second;
  return r;
}
Value S(const char* s) { return Value::Of(s); }
Value N(double d) { return Value::Of(d); }

std::vector<Row::Kind> Kinds(const ScheduleResult& r) {
  std::vector<Row::Kind> k;
  for (auto& x : r.rows) k.push_back(x.kind);
  return k;
}
bool Cells(const Row& r, const std::vector<std::string>& want) { return r.cells == want; }

// D8-REVIT-SCHED-0002: "Add fields from the category and define calculated values by formula over parameters." Sketch: width times
// height equals the stored area. Doors (W, H, stored Area): 0.9 x 2.1 = 1.89, 1.0 x 2.1 = 2.1, 0.8 x 2.0 = 1.6. Constants:
// 2 + 3 * 4 ^ 2 / 8 = 2 + 48 / 8 = 8; -2 ^ 2 = -4; 2 ^ 3 ^ 2 = 2 ^ 9 = 512; (2 + 3) * 4 = 20; sqrt(16) + abs(-3) = 7; min(3,1,2) = 1.
// if(W > 0.85, 1, 0) is 1 for 0.9 and 0 for 0.8; W / 0 and an unknown parameter are not evaluated; "W *" is a syntax error.
D8_EXTRA_TEST(Rw3DocScheduleCalculatedValue) {
  ScheduleDef d;
  d.fields = {P("W", 2), P("H", 2), P("Area", 2)};
  Field calc; calc.name = "WxH"; calc.kind = FieldKind::Formula; calc.formula = "W*H"; calc.decimals = 2;
  Field big; big.name = "Wide"; big.kind = FieldKind::Formula; big.formula = "if(W>0.85,1,0)"; big.decimals = 0;
  Field bad; bad.name = "Bad"; bad.kind = FieldKind::Formula; bad.formula = "W/Zero"; bad.decimals = 1;
  d.fields.push_back(calc); d.fields.push_back(big); d.fields.push_back(bad);
  std::vector<Record> recs = {Rec({{"W", N(0.9)}, {"H", N(2.1)}, {"Area", N(1.89)}, {"Zero", N(0)}}),
                              Rec({{"W", N(1.0)}, {"H", N(2.1)}, {"Area", N(2.1)}, {"Zero", N(0)}}),
                              Rec({{"W", N(0.8)}, {"H", N(2.0)}, {"Area", N(1.6)}, {"Zero", N(0)}})};
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(r.ok && r.rows.size() == 3, "calculated value: three door rows");
  d8test::Check(Cells(r.rows[0], {"0.90", "2.10", "1.89", "1.89", "1", "-"}) && Cells(r.rows[1], {"1.00", "2.10", "2.10", "2.10", "1", "-"}) &&
                    Cells(r.rows[2], {"0.80", "2.00", "1.60", "1.60", "0", "-"}),
                "calculated value: W x H equals the stored area, if() picks 1 / 1 / 0, division by zero shows not-evaluated");
  Record none;
  auto ev = [&](const char* f, double& out) { Value v; bool ok = Formula::Eval(f, none, v); out = v.n; return ok && v.kind == Value::Num; };
  double x = 0;
  d8test::Check(ev("2+3*4^2/8", x) && Near(x, 8), "formula: 2 + 3 * 4^2 / 8 = 8");
  d8test::Check(ev("-2^2", x) && Near(x, -4), "formula: -2^2 = -4");
  d8test::Check(ev("2^3^2", x) && Near(x, 512), "formula: 2^3^2 = 512 (right associative)");
  d8test::Check(ev("(2+3)*4", x) && Near(x, 20), "formula: (2+3)*4 = 20");
  d8test::Check(ev("sqrt(16)+abs(-3)", x) && Near(x, 7), "formula: sqrt(16) + abs(-3) = 7");
  d8test::Check(ev("min(3,1,2)", x) && Near(x, 1) && ev("max(3,1,2)", x) && Near(x, 3), "formula: min and max of three");
  d8test::Check(ev("round(2.5)", x) && Near(x, 3), "formula: round(2.5) = 3");
  Value v;
  d8test::Check(!Formula::Eval("W*", recs[0], v) && !Formula::Eval("(1+2", recs[0], v) && !Formula::Eval("nofn(1)", recs[0], v), "formula: syntax errors are reported");
  d8test::Check(Formula::Eval("Missing+1", recs[0], v) && !v.Defined(), "formula: an unknown parameter is not evaluated");
}

// D8-REVIT-SCHED-0003: percentage of a field's total, overall or within a grouping field. Areas: L1 {10, 30}, L2 {60, 100}, L3 {0}.
// Overall total over all four non-empty rows is 200: 5, 15, 30, 50 percent (sum 100). By Level: L1 total 40 -> 25 and 75; L2 total 160
// -> 37.5 and 62.5 (each group sums to 100); L3 total 0 -> not evaluated.
D8_EXTRA_TEST(Rw3DocSchedulePercentOfTotal) {
  std::vector<Record> recs = {Rec({{"Level", S("L1")}, {"Area", N(10)}}), Rec({{"Level", S("L1")}, {"Area", N(30)}}),
                              Rec({{"Level", S("L2")}, {"Area", N(60)}}), Rec({{"Level", S("L2")}, {"Area", N(100)}})};
  ScheduleDef d;
  Field pct; pct.name = "Share"; pct.kind = FieldKind::Percent; pct.percentOf = "Area"; pct.decimals = 1;
  d.fields = {P("Level"), P("Area", 0), pct};
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(r.rows.size() == 4 && r.rows[0].cells[2] == "5.0" && r.rows[1].cells[2] == "15.0" && r.rows[2].cells[2] == "30.0" && r.rows[3].cells[2] == "50.0",
                "percent: overall shares are 5, 15, 30 and 50");
  d.fields[2].percentBy = "Level";
  r = BuildSchedule(d, recs);
  d8test::Check(r.rows[0].cells[2] == "25.0" && r.rows[1].cells[2] == "75.0" && r.rows[2].cells[2] == "37.5" && r.rows[3].cells[2] == "62.5",
                "percent: shares within the Level group are 25 / 75 and 37.5 / 62.5");
  recs.push_back(Rec({{"Level", S("L3")}, {"Area", N(0)}}));
  r = BuildSchedule(d, recs);
  d8test::Check(r.rows.size() == 5 && r.rows[4].cells[2] == "-", "percent: a group whose total is zero is not evaluated");
  // a filter changes the base: only L2 rows -> overall shares 60 / 160 = 37.5 and 62.5
  d.fields[2].percentBy.clear();
  Condition c; c.field = "Level"; c.op = Op::Eq; c.value = S("L2");
  d.filters = {c};
  r = BuildSchedule(d, recs);
  d8test::Check(r.rows.size() == 2 && r.rows[0].cells[2] == "37.5" && r.rows[1].cells[2] == "62.5", "percent: the base is the filtered set");
}

// D8-REVIT-SCHED-0004: up to eight case-sensitive conditions, all must pass. Doors: (Type, Width)
// 0 "Single" 0.9; 1 "single" 1.0; 2 "Single" 0.8; 3 "Double" 1.8; 4 "Single" 1.0; 5 "Single" 0.9.
// Type = "Single" case-sensitive: indexes 0, 2, 4, 5 (not 1). With Width >= 0.9 as well: 0, 4, 5. With a third condition Width < 1.0: 0, 5.
// Nine conditions are refused. Contains "ing" matches 0, 1, 2, 4, 5; begins with "Do" matches 3; has no value on an absent field matches all.
D8_EXTRA_TEST(Rw3DocScheduleFiltersAllMustPass) {
  std::vector<Record> recs;
  const char* types[] = {"Single", "single", "Single", "Double", "Single", "Single"};
  double widths[] = {0.9, 1.0, 0.8, 1.8, 1.0, 0.9};
  for (int i = 0; i < 6; ++i) recs.push_back(Rec({{"Id", N(i)}, {"Type", S(types[i])}, {"Width", N(widths[i])}}));
  ScheduleDef d;
  d.fields = {P("Id", 0)};
  auto ids = [&](const ScheduleDef& def) { std::string s; for (auto& r : BuildSchedule(def, recs).rows) s += r.cells[0]; return s; };
  Condition a{"Type", Op::Eq, S("Single")}, b{"Width", Op::Ge, N(0.9)}, c{"Width", Op::Lt, N(1.0)};
  d.filters = {a};
  d8test::Check(ids(d) == "0245", "filters: Type = Single is case-sensitive: rows 0 2 4 5");
  d.filters = {a, b};
  d8test::Check(ids(d) == "045", "filters: plus Width >= 0.9: rows 0 4 5");
  d.filters = {a, b, c};
  d8test::Check(ids(d) == "05", "filters: plus Width < 1.0: rows 0 5");
  d.filters = {{"Type", Op::Contains, S("ing")}};
  d8test::Check(ids(d) == "01245", "filters: contains 'ing'");
  d.filters = {{"Type", Op::BeginsWith, S("Do")}, {"Nothing", Op::HasNoValue, Value()}};
  d8test::Check(ids(d) == "3", "filters: begins with 'Do' and a field with no value");
  d.filters.assign(8, a);
  d8test::Check(BuildSchedule(d, recs).ok, "filters: eight conditions are accepted");
  d.filters.assign(9, a);
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(!r.ok && r.rows.empty(), "filters: nine conditions are refused");
}

// D8-REVIT-SCHED-0005: sort, group headers / footers / blank lines, itemize or collapse. Six elements (Level, Type, Area):
// (L1,A,2) (L1,B,3) (L2,A,5) (L1,A,2) (L2,B,7) (L2,A,5). Sorted by Level then Type, itemized, with header + footer + blank on Level and
// a footer on Type, the rows are: H(L1) 2 2 F(A: 2 items, 4) 3 F(B: 1, 3) F(L1: 3, 7) blank H(L2) 5 5 F(A: 2, 10) 7 F(B: 1, 7) F(L2: 3, 17)
// blank, grand total 6 items and 24. Collapsed (itemize off, with a Count column): the two (L1,A,2) rows and the two (L2,A,5) rows merge
// into rows with count 2, giving 4 data rows with counts 2 1 2 1, while the group totals still add the records (7 and 17).
D8_EXTRA_TEST(Rw3DocScheduleGroupFootersAndCollapse) {
  std::vector<Record> recs = {Rec({{"Level", S("L1")}, {"Type", S("A")}, {"Area", N(2)}}), Rec({{"Level", S("L1")}, {"Type", S("B")}, {"Area", N(3)}}),
                              Rec({{"Level", S("L2")}, {"Type", S("A")}, {"Area", N(5)}}), Rec({{"Level", S("L1")}, {"Type", S("A")}, {"Area", N(2)}}),
                              Rec({{"Level", S("L2")}, {"Type", S("B")}, {"Area", N(7)}}), Rec({{"Level", S("L2")}, {"Type", S("A")}, {"Area", N(5)}})};
  ScheduleDef d;
  Field area = P("Area", 0); area.aggregate = Aggregate::Total;
  d.fields = {P("Level"), P("Type"), area};
  SortKey lv; lv.field = "Level"; lv.header = lv.footer = lv.blankAfter = true;
  SortKey ty; ty.field = "Type"; ty.footer = true;
  d.sort = {lv, ty};
  d.grandTotal = true;
  ScheduleResult r = BuildSchedule(d, recs);
  using K = Row::Kind;
  std::vector<K> want = {K::GroupHeader, K::Data, K::Data, K::GroupFooter, K::Data, K::GroupFooter, K::GroupFooter, K::Blank,
                         K::GroupHeader, K::Data, K::Data, K::GroupFooter, K::Data, K::GroupFooter, K::GroupFooter, K::Blank, K::GrandTotal};
  d8test::Check(r.ok && Kinds(r) == want, "grouping: header / data / footer / blank sequence of two sort levels");
  d8test::Check(r.rows[0].label == "L1" && r.rows[3].label == "A" && r.rows[3].count == 2 && Cells(r.rows[3], {"", "", "4"}), "grouping: Type A footer has 2 items and total 4");
  d8test::Check(r.rows[5].label == "B" && r.rows[5].count == 1 && r.rows[5].cells[2] == "3", "grouping: Type B footer 1 item, 3");
  d8test::Check(r.rows[6].label == "L1" && r.rows[6].count == 3 && r.rows[6].cells[2] == "7", "grouping: Level L1 footer 3 items, 7");
  d8test::Check(r.rows[11].count == 2 && r.rows[11].cells[2] == "10" && r.rows[13].cells[2] == "7" && r.rows[14].count == 3 && r.rows[14].cells[2] == "17", "grouping: Level L2 footers 10, 7, 17");
  d8test::Check(r.rows[16].count == 6 && r.rows[16].cells[2] == "24", "grouping: grand total 6 items, 24");
  d.itemize = false;
  Field cnt; cnt.name = "Count"; cnt.kind = FieldKind::Count;
  d.fields.push_back(cnt);
  r = BuildSchedule(d, recs);
  int data = 0;
  std::string counts;
  for (auto& x : r.rows) if (x.kind == K::Data) { ++data; counts += x.cells[3]; }
  d8test::Check(data == 4 && counts == "2121", "collapse: four rows with counts 2 1 2 1");
  d8test::Check(r.rows[5].cells[2] == "7" && r.rows[12].cells[2] == "17" && r.rows[14].cells[2] == "24", "collapse: totals still add the records");
  // sorting descending on a numeric field
  ScheduleDef s;
  s.fields = {P("Area", 0)};
  SortKey desc; desc.field = "Area"; desc.descending = true;
  s.sort = {desc};
  r = BuildSchedule(s, recs);
  std::string order;
  for (auto& x : r.rows) order += x.cells[0];
  d8test::Check(order == "755322", "sort: descending by area gives 7 5 5 3 2 2");
}

// D8-REVIT-SCHED-0020: units format, totals, min / max. Lengths 1.2, 2.3, 0.5 m. Sum 4.0, min 0.5, max 2.3. In millimetres with no
// decimals: "4000 mm", "500 mm", "2300 mm"; in metres with one decimal: "4.0 m".
D8_EXTRA_TEST(Rw3DocScheduleTotalsHonourUnitFormat) {
  std::vector<Record> recs = {Rec({{"L", N(1.2)}}), Rec({{"L", N(2.3)}}), Rec({{"L", N(0.5)}})};
  auto grand = [&](Aggregate agg, double scale, int dec, const char* unit) {
    ScheduleDef d;
    Field f = P("L", dec); f.aggregate = agg; f.scale = scale; f.unit = unit; f.showUnit = true;
    d.fields = {f};
    d.grandTotal = true;
    ScheduleResult r = BuildSchedule(d, recs);
    return r.rows.back().cells[0];
  };
  d8test::Check(grand(Aggregate::Total, 1000, 0, " mm") == "4000 mm", "totals: sum in mm");
  d8test::Check(grand(Aggregate::Min, 1000, 0, " mm") == "500 mm", "totals: minimum in mm");
  d8test::Check(grand(Aggregate::Max, 1000, 0, " mm") == "2300 mm", "totals: maximum in mm");
  d8test::Check(grand(Aggregate::Total, 1, 1, " m") == "4.0 m", "totals: sum in m with one decimal");
  ScheduleDef d;
  Field f = P("L", 0); f.scale = 1000; f.unit = " mm"; f.showUnit = true;
  d.fields = {f};
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(r.rows[0].cells[0] == "1200 mm" && r.rows[1].cells[0] == "2300 mm" && r.rows[2].cells[0] == "500 mm", "units: row values carry the unit format");
}

// D8-REVIT-SCHED-0027: combine parameters into one column with a separator, prefixes and suffixes. Door width 0.9, height 2.1,
// thickness 0.04 joined by "x": "0.9x2.1x0.04". With prefixes W and H, one decimal, separator "-": "W0.9-H2.1". With suffix "m" and two
// decimals: "0.90m / 2.10m" using separator " / ".
D8_EXTRA_TEST(Rw3DocScheduleCombinedParameters) {
  std::vector<Record> recs = {Rec({{"W", N(0.9)}, {"H", N(2.1)}, {"T", N(0.04)}})};
  auto build = [&](const std::string& sep, std::vector<CombinedPart> parts) {
    ScheduleDef d;
    Field f; f.name = "Size"; f.kind = FieldKind::Combined; f.separator = sep; f.parts = parts;
    d.fields = {f};
    return BuildSchedule(d, recs).rows[0].cells[0];
  };
  d8test::Check(build("x", {{"W", "", "", -1}, {"H", "", "", -1}, {"T", "", "", -1}}) == "0.9x2.1x0.04", "combined: width x height x thickness");
  d8test::Check(build("-", {{"W", "W", "", 1}, {"H", "H", "", 1}}) == "W0.9-H2.1", "combined: prefixes and one decimal");
  d8test::Check(build(" / ", {{"W", "", "m", 2}, {"H", "", "m", 2}}) == "0.90m / 2.10m", "combined: suffixes and two decimals");
}

// D8-REVIT-SCHED-0024: door marks from a numbering template. Doors in placement order d1..d6 opening into rooms
// R101 R102 R101 R101 R102 R103. Partitioned with letters and no separator: R101A R102A R101B R101C R102B R103A.
// Sequential fallback with prefix D and width 3: D001..D006. With D002 already taken the sequence skips it: D001 D003 D004 D005 D006 D007.
// Spreadsheet letters: 1 A, 26 Z, 27 AA, 52 AZ, 53 BA, 702 ZZ, 703 AAA.
D8_EXTRA_TEST(Rw3DocDoorMarkNumbering) {
  std::vector<MarkItem> items = {{"d1", "R101"}, {"d2", "R102"}, {"d3", "R101"}, {"d4", "R101"}, {"d5", "R102"}, {"d6", "R103"}};
  MarkTemplate t; t.partitioned = true; t.letters = true;
  auto m = NumberMarks(items, t);
  d8test::Check(m["d1"] == "R101A" && m["d2"] == "R102A" && m["d3"] == "R101B" && m["d4"] == "R101C" && m["d5"] == "R102B" && m["d6"] == "R103A",
                "marks: sequence letters restart in every room");
  MarkTemplate q; q.prefix = "D"; q.width = 3;
  m = NumberMarks(items, q);
  d8test::Check(m["d1"] == "D001" && m["d2"] == "D002" && m["d6"] == "D006", "marks: sequential D001..D006");
  m = NumberMarks(items, q, {"D002"});
  d8test::Check(m["d1"] == "D001" && m["d2"] == "D003" && m["d3"] == "D004" && m["d4"] == "D005" && m["d5"] == "D006" && m["d6"] == "D007", "marks: a taken mark is skipped");
  std::set<std::string> uniq;
  for (auto& kv : m) uniq.insert(kv.second);
  d8test::Check(uniq.size() == items.size(), "marks: every mark is unique");
  d8test::Check(LettersOf(1) == "A" && LettersOf(26) == "Z" && LettersOf(27) == "AA" && LettersOf(52) == "AZ" && LettersOf(53) == "BA" && LettersOf(702) == "ZZ" && LettersOf(703) == "AAA",
                "marks: spreadsheet letters");
  MarkTemplate s; s.partitioned = true; s.prefix = "N-"; s.partitionSeparator = "."; s.start = 10;
  m = NumberMarks({{"a", "X"}, {"b", "X"}}, s);
  d8test::Check(m["a"] == "N-X.10" && m["b"] == "N-X.11", "marks: prefix, separator and start value");
}

// D8-REVIT-SCHED-0008 and D8-ARCHICAD-SCHED-0024: material takeoff per layer with mass and embodied carbon.
// Wall W1: face 10 x 3 = 30 m2 minus a 1 x 2 door = 28 m2; layers brick 0.1, insulation 0.05, gypsum 0.0125. Volumes 2.8, 1.4, 0.35.
// Brick 1800 kg/m3 -> 5040 kg, carbon 0.22 kg/kg -> 1108.8; insulation 30 kg/m3 -> 42 kg, no carbon indicator (not evaluated); gypsum
// 800 -> 280 kg, carbon 0.12 -> 33.6. Wall W2: 20 m2 of brick 0.1: volume 2.0, mass 3600, carbon 792. Per material (collapsed, footer):
// brick area 48, volume 4.8, mass 8640, carbon 1900.8. Grand mass 8640 + 42 + 280 = 8962; grand carbon 1900.8 + 33.6 = 1934.4 (the
// undefined indicator adds nothing). Cost: brick 400 per m3 -> 4.8 x 400 = 1920.
D8_EXTRA_TEST(Rw3DocMaterialTakeoffMassAndCarbon) {
  std::vector<MaterialDef> mats(3);
  mats[0].name = "Brick"; mats[0].density = 1800; mats[0].hasCarbon = true; mats[0].carbonPerKg = 0.22; mats[0].costPerM3 = 400;
  mats[1].name = "Insulation"; mats[1].density = 30;
  mats[2].name = "Gypsum"; mats[2].density = 800; mats[2].hasCarbon = true; mats[2].carbonPerKg = 0.12;
  LayeredElement w1; w1.id = "W1"; w1.category = "Wall"; w1.faceArea = 30; w1.openingArea = 2;
  w1.layers = {{"Brick", 0.1}, {"Insulation", 0.05}, {"Gypsum", 0.0125}};
  LayeredElement w2; w2.id = "W2"; w2.category = "Wall"; w2.faceArea = 20; w2.layers = {{"Brick", 0.1}};
  std::vector<Record> recs = TakeoffRecords({w1, w2}, mats);
  d8test::Check(recs.size() == 4, "takeoff: one record per element layer");
  d8test::Check(Near(recs[0]["Area"].n, 28) && Near(recs[0]["Volume"].n, 2.8) && Near(recs[1]["Volume"].n, 1.4) && Near(recs[2]["Volume"].n, 0.35), "takeoff: layer volumes 2.8, 1.4, 0.35");
  d8test::Check(Near(recs[0]["Mass"].n, 5040, 1e-6) && Near(recs[1]["Mass"].n, 42, 1e-6) && Near(recs[2]["Mass"].n, 280, 1e-6), "takeoff: masses 5040, 42, 280 kg");
  d8test::Check(Near(recs[0]["EmbodiedCarbon"].n, 1108.8, 1e-6) && !recs[1].count("EmbodiedCarbon") && Near(recs[2]["EmbodiedCarbon"].n, 33.6, 1e-6), "takeoff: carbon 1108.8, not evaluated, 33.6");
  d8test::Check(Near(recs[3]["Mass"].n, 3600, 1e-6) && Near(recs[3]["EmbodiedCarbon"].n, 792, 1e-6), "takeoff: W2 mass 3600 and carbon 792");
  ScheduleDef d;
  Field mat = P("Material");
  Field area = P("Area", 1); area.aggregate = Aggregate::Total;
  Field vol = P("Volume", 2); vol.aggregate = Aggregate::Total;
  Field mass = P("Mass", 0); mass.aggregate = Aggregate::Total;
  Field co2 = P("EmbodiedCarbon", 1); co2.aggregate = Aggregate::Total;
  Field cost = P("Cost", 0); cost.aggregate = Aggregate::Total;
  d.fields = {mat, area, vol, mass, co2, cost};
  SortKey k; k.field = "Material"; k.footer = true;
  d.sort = {k};
  d.grandTotal = true;
  ScheduleResult r = BuildSchedule(d, recs);
  const Row* brick = nullptr; const Row* ins = nullptr; const Row* grand = nullptr;
  for (auto& x : r.rows) {
    if (x.kind == Row::GroupFooter && x.label == "Brick") brick = &x;
    if (x.kind == Row::GroupFooter && x.label == "Insulation") ins = &x;
    if (x.kind == Row::GrandTotal) grand = &x;
  }
  d8test::Check(brick && Cells(*brick, {"", "48.0", "4.80", "8640", "1900.8", "1920"}) && brick->count == 2, "takeoff: brick footer area 48, volume 4.8, mass 8640, carbon 1900.8, cost 1920");
  d8test::Check(ins && ins->cells[4] == "-" && ins->cells[3] == "42", "takeoff: insulation footer carbon not evaluated, mass 42");
  d8test::Check(grand && grand->cells[3] == "8962" && grand->cells[4] == "1934.4", "takeoff: grand mass 8962 and carbon 1934.4");
}

// D8-REVIT-SCHED-0017: delimited export. Rows (Level, Area): (L1,2) (L1,3) (L2,5), sorted by Level with header and footer.
// Without group rows, delimiter ';' and qualifier '"': header line, then the three data lines. With group rows: header "L1" line, the
// two data lines, footer "L1: 2" with total 5, then L2. A cell holding a double quote is doubled inside the qualifier. With no
// qualifier and a comma delimiter the cells appear bare.
D8_EXTRA_TEST(Rw3DocScheduleDelimitedExport) {
  std::vector<Record> recs = {Rec({{"Level", S("L1")}, {"Area", N(2)}}), Rec({{"Level", S("L1")}, {"Area", N(3)}}), Rec({{"Level", S("L2")}, {"Area", N(5)}})};
  ScheduleDef d;
  Field a = P("Area", 0); a.aggregate = Aggregate::Total;
  d.fields = {P("Level"), a};
  SortKey k; k.field = "Level"; k.header = k.footer = true;
  d.sort = {k};
  ScheduleResult r = BuildSchedule(d, recs);
  ExportOptions o; o.delimiter = ';';
  d8test::Check(ExportDelimited(d, r, o) == "\"Level\";\"Area\"\n\"L1\";\"2\"\n\"L1\";\"3\"\n\"L2\";\"5\"\n", "export: data rows only, header row, ; and quote qualifier");
  o.headerRow = false; o.groupRows = true; o.qualifier = 0; o.delimiter = ',';
  d8test::Check(ExportDelimited(d, r, o) == "L1,\nL1,2\nL1,3\nL1: 2,5\nL2,\nL2,5\nL2: 1,5\n", "export: group header and footer rows, no qualifier, comma");
  recs.push_back(Rec({{"Level", S("5\" slab")}, {"Area", N(1)}}));
  ScheduleDef plain; plain.fields = {P("Level"), P("Area", 0)};
  ScheduleResult r2 = BuildSchedule(plain, {recs[3]});
  ExportOptions q;
  d8test::Check(ExportDelimited(plain, r2, q) == "\"Level\"\t\"Area\"\n\"5\"\" slab\"\t\"1\"\n", "export: a quote inside a cell is doubled");
}

// D8-ARCHICAD-SCHED-0005: criteria with AND / OR and parentheses. Elements (Layer, Height), indexes 0..5:
// (A,1) (A,3) (B,1) (B,3) (C,3) (C,1).
// "Layer=A OR Layer=B AND Height>2" with AND binding tighter: A or (B and H>2) -> 0, 1, 3.
// "(Layer=A OR Layer=B) AND Height>2" -> 1, 3. "((A OR B) AND H>2) OR C" -> 1, 3, 4, 5.
// Unbalanced groups ("(" never closed, an extra ")", or ")" before "(") give a warning and no rows.
D8_EXTRA_TEST(Rw3DocCriteriaAndOrParentheses) {
  const char* layer[] = {"A", "A", "B", "B", "C", "C"};
  double h[] = {1, 3, 1, 3, 3, 1};
  std::vector<Record> recs;
  for (int i = 0; i < 6; ++i) recs.push_back(Rec({{"Layer", S(layer[i])}, {"Height", N(h[i])}}));
  auto row = [](const char* f, Op op, Value v, int open, int close, bool orJoin) {
    CriterionRow r; r.cond = {f, op, v}; r.open = open; r.close = close; r.orWithPrevious = orJoin; return r;
  };
  auto idx = [&](const CriteriaResult& c) { std::string s; for (size_t i : c.matches) s += std::to_string(i); return s; };
  CriteriaResult c = FilterByCriteria({row("Layer", Op::Eq, S("A"), 0, 0, false), row("Layer", Op::Eq, S("B"), 0, 0, true), row("Height", Op::Gt, N(2), 0, 0, false)}, recs);
  d8test::Check(c.valid && idx(c) == "013", "criteria: A OR B AND H>2 (AND first) -> 0, 1, 3");
  c = FilterByCriteria({row("Layer", Op::Eq, S("A"), 1, 0, false), row("Layer", Op::Eq, S("B"), 0, 1, true), row("Height", Op::Gt, N(2), 0, 0, false)}, recs);
  d8test::Check(c.valid && idx(c) == "13", "criteria: (A OR B) AND H>2 -> 1, 3");
  c = FilterByCriteria({row("Layer", Op::Eq, S("A"), 2, 0, false), row("Layer", Op::Eq, S("B"), 0, 1, true), row("Height", Op::Gt, N(2), 0, 1, false), row("Layer", Op::Eq, S("C"), 0, 0, true)}, recs);
  d8test::Check(c.valid && idx(c) == "1345", "criteria: ((A OR B) AND H>2) OR C -> 1, 3, 4, 5");
  c = FilterByCriteria({row("Layer", Op::Eq, S("A"), 1, 0, false), row("Height", Op::Gt, N(2), 0, 0, false)}, recs);
  d8test::Check(!c.valid && c.matches.empty() && !c.warning.empty(), "criteria: an unclosed parenthesis is invalid");
  c = FilterByCriteria({row("Layer", Op::Eq, S("A"), 0, 1, false)}, recs);
  d8test::Check(!c.valid, "criteria: an extra closing parenthesis is invalid");
  c = FilterByCriteria({row("Layer", Op::Eq, S("A"), 0, 1, false), row("Height", Op::Gt, N(2), 1, 0, false)}, recs);
  d8test::Check(!c.valid, "criteria: a closing parenthesis before its opening one is invalid");
  c = FilterByCriteria({}, recs);
  d8test::Check(c.valid && c.matches.size() == 6, "criteria: no criteria lists everything");
}

// D8-ARCHICAD-SCHED-0006: classification tests (is, is not, under) and defined / undefined tests on a mixed set.
// 0 class "Wall" FireRating 60; 1 "Wall/Exterior" no rating; 2 "Wall/Interior" FireRating 30; 3 "Slab/Floor" no rating; 4 no class, no rating.
// Is "Wall" -> 0. Is not "Wall" -> 1, 2, 3, 4 (an element without classification is not "Wall"). Under "Wall" (strict descendants) -> 1, 2.
// Under "Slab" -> 3. FireRating defined -> 0, 2; undefined -> 1, 3, 4. A prefix such as "Wal" is not a classification ancestor.
D8_EXTRA_TEST(Rw3DocCriteriaClassificationAndDefined) {
  std::vector<Record> recs = {Rec({{"Class", S("Wall")}, {"Fire", N(60)}}), Rec({{"Class", S("Wall/Exterior")}}), Rec({{"Class", S("Wall/Interior")}, {"Fire", N(30)}}),
                              Rec({{"Class", S("Slab/Floor")}}), Rec({})};
  auto run = [&](const char* f, Op op, Value v) {
    CriterionRow r; r.cond = {f, op, v};
    std::string s;
    for (size_t i : FilterByCriteria({r}, recs).matches) s += std::to_string(i);
    return s;
  };
  d8test::Check(run("Class", Op::ClassIs, S("Wall")) == "0", "classification: is Wall -> 0");
  d8test::Check(run("Class", Op::ClassIsNot, S("Wall")) == "1234", "classification: is not Wall -> 1 2 3 4");
  d8test::Check(run("Class", Op::ClassUnder, S("Wall")) == "12", "classification: under Wall -> 1 2");
  d8test::Check(run("Class", Op::ClassUnder, S("Slab")) == "3", "classification: under Slab -> 3");
  d8test::Check(run("Class", Op::ClassUnder, S("Wal")) == "", "classification: a name prefix is not an ancestor");
  d8test::Check(run("Fire", Op::HasValue, Value()) == "02", "property defined -> 0 2");
  d8test::Check(run("Fire", Op::HasNoValue, Value()) == "134", "property undefined -> 1 3 4");
  d8test::Check(run("Fire", Op::Ge, N(30)) == "02", "property >= 30 -> 0 2 (undefined never matches)");
}

// D8-ARCHICAD-SCHED-0009 and D8-ARCHICAD-SCHED-0010: sort ascending and descending, subdivision totals, grand totals with prefix and
// suffix. Doors (Type, Width): (B,1.0) (A,0.9) (B,1.2) (A,0.8) (C,1.0). Sorted by Type ascending then Width descending:
// A 0.9, A 0.8, footer A (2 items, 1.7); B 1.2, B 1.0, footer B (2, 2.2); C 1.0, footer C (1, 1.0); grand total 5 items, 4.9.
// Types descending: C, B, B, A, A. The total cells carry the prefix "= " and suffix " m".
D8_EXTRA_TEST(Rw3DocScheduleSubdivisionAndGrandTotals) {
  std::vector<Record> recs = {Rec({{"Type", S("B")}, {"Width", N(1.0)}}), Rec({{"Type", S("A")}, {"Width", N(0.9)}}), Rec({{"Type", S("B")}, {"Width", N(1.2)}}),
                              Rec({{"Type", S("A")}, {"Width", N(0.8)}}), Rec({{"Type", S("C")}, {"Width", N(1.0)}})};
  ScheduleDef d;
  Field w = P("Width", 1); w.aggregate = Aggregate::Total; w.totalPrefix = "= "; w.totalSuffix = " m";
  d.fields = {P("Type"), w};
  SortKey t; t.field = "Type"; t.footer = true;
  SortKey wd; wd.field = "Width"; wd.descending = true;
  d.sort = {t, wd};
  d.grandTotal = true;
  ScheduleResult r = BuildSchedule(d, recs);
  using K = Row::Kind;
  d8test::Check(Kinds(r) == std::vector<K>({K::Data, K::Data, K::GroupFooter, K::Data, K::Data, K::GroupFooter, K::Data, K::GroupFooter, K::GrandTotal}), "subdivision: row kinds");
  d8test::Check(r.rows[0].cells[1] == "0.9" && r.rows[1].cells[1] == "0.8" && r.rows[3].cells[1] == "1.2" && r.rows[4].cells[1] == "1.0", "subdivision: width descending inside each type");
  d8test::Check(r.rows[2].count == 2 && r.rows[2].cells[1] == "= 1.7 m" && r.rows[5].count == 2 && r.rows[5].cells[1] == "= 2.2 m" && r.rows[7].count == 1 && r.rows[7].cells[1] == "= 1.0 m",
                "subdivision: footers 1.7, 2.2, 1.0 with prefix and suffix");
  d8test::Check(r.rows[8].count == 5 && r.rows[8].cells[1] == "= 4.9 m", "grand total: 5 items, 4.9");
  d.sort[0].descending = true;
  r = BuildSchedule(d, recs);
  std::string types;
  for (auto& x : r.rows) if (x.kind == K::GroupFooter) types += x.label;
  d8test::Check(types == "CBA", "subdivision: types descending C B A");
}

// D8-ARCHICAD-SCHED-0011: merge identical records. Ten identical doors (Type D1, Width 0.9, differing Mark D1..D10) with a Count field.
// With Mark flagged "any value": one row, count 10. Without the flag the marks differ and nothing merges (10 rows of count 1).
// Nine identical doors and one different: two rows, counts 9 and 1, in order of first appearance.
D8_EXTRA_TEST(Rw3DocScheduleMergeIdenticalRecords) {
  std::vector<Record> recs;
  for (int i = 1; i <= 10; ++i) recs.push_back(Rec({{"Type", S("D1")}, {"Width", N(0.9)}, {"Mark", S(("D" + std::to_string(i)).c_str())}}));
  ScheduleDef d;
  Field mark = P("Mark"); mark.mergeAny = true;
  Field cnt; cnt.name = "Qty"; cnt.kind = FieldKind::Count;
  d.fields = {P("Type"), P("Width", 1), mark, cnt};
  d.itemize = false;
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(r.rows.size() == 1 && r.rows[0].count == 10 && r.rows[0].cells[3] == "10" && r.rows[0].cells[0] == "D1" && r.rows[0].cells[1] == "0.9", "merge: ten identical doors give one row of 10");
  d.fields[2].mergeAny = false;
  r = BuildSchedule(d, recs);
  d8test::Check(r.rows.size() == 10 && r.rows[0].cells[3] == "1", "merge: differing marks keep the rows apart");
  d.fields[2].mergeAny = true;
  recs[4]["Type"] = S("D2");
  r = BuildSchedule(d, recs);
  d8test::Check(r.rows.size() == 2 && r.rows[0].cells[3] == "9" && r.rows[0].cells[0] == "D1" && r.rows[1].cells[3] == "1" && r.rows[1].cells[0] == "D2", "merge: 9 + 1 rows in order of first appearance");
}

// D8-ARCHICAD-SCHED-0029 (display of merged fields) shares the engine: widths 0.9, 0.9, 1.0, 1.0 merged. Sum 3.8, list "0.9 (2), 1.0 (2)",
// list without quantities "0.9, 1.0", uniform mode shows nothing for disagreeing values and the value when they agree.
D8_EXTRA_TEST(Rw3DocScheduleMergedFieldDisplay) {
  std::vector<Record> recs;
  double w[] = {0.9, 0.9, 1.0, 1.0};
  for (double x : w) recs.push_back(Rec({{"Type", S("D")}, {"Width", N(x)}}));
  auto show = [&](MergeShow m, bool qty) {
    ScheduleDef d;
    Field f = P("Width", 1); f.mergeAny = true; f.mergeShow = m; f.mergeAppendQuantity = qty;
    d.fields = {P("Type"), f};
    d.itemize = false;
    ScheduleResult r = BuildSchedule(d, recs);
    return r.rows.size() == 1 ? r.rows[0].cells[1] : std::string("rows=") + std::to_string(r.rows.size());
  };
  d8test::Check(show(MergeShow::Sum, true) == "3.8", "merged display: sum of values 3.8");
  d8test::Check(show(MergeShow::List, true) == "0.9 (2), 1.0 (2)", "merged display: list with quantities");
  d8test::Check(show(MergeShow::List, false) == "0.9, 1.0", "merged display: list without quantities");
  d8test::Check(show(MergeShow::Uniform, true) == "", "merged display: uniform shows nothing when values differ");
  recs[2]["Width"] = N(0.9); recs[3]["Width"] = N(0.9);
  d8test::Check(show(MergeShow::Uniform, true) == "0.9", "merged display: uniform shows the common value");
}

// D8-ARCHICAD-SCHED-0028: custom text for zero and not-evaluated values, independent for values, totals and grand totals.
// Group G1 has Qty {0, undefined}; G2 has {5}; G3 has {undefined}. Zero text for values "none", for totals "zero total", for grand totals
// "zero grand"; not-evaluated text "?". Rows: "none", "?"; footer G1 = 0 -> "zero total"; row "5"; footer G2 "5"; G3 row "?", footer
// not evaluated "?". With the filter Group = G1 the grand total is 0 -> "zero grand". The default not-evaluated text is a dash.
D8_EXTRA_TEST(Rw3DocScheduleZeroAndNotEvaluatedText) {
  std::vector<Record> recs = {Rec({{"Group", S("G1")}, {"Qty", N(0)}}), Rec({{"Group", S("G1")}}), Rec({{"Group", S("G2")}, {"Qty", N(5)}}), Rec({{"Group", S("G3")}})};
  ScheduleDef d;
  Field q = P("Qty", 0); q.aggregate = Aggregate::Total;
  q.zeroValueText = true; q.zeroValue = "none"; q.zeroTotalText = true; q.zeroTotal = "zero total"; q.zeroGrandText = true; q.zeroGrand = "zero grand"; q.notEvaluated = "?";
  d.fields = {P("Group"), q};
  SortKey k; k.field = "Group"; k.footer = true;
  d.sort = {k};
  d.grandTotal = true;
  ScheduleResult r = BuildSchedule(d, recs);
  std::vector<std::string> got;
  for (auto& x : r.rows) got.push_back(x.cells[1]);
  d8test::Check(got == std::vector<std::string>({"none", "?", "zero total", "5", "5", "?", "?", "5"}), "zero text: values, totals and grand total use their own texts");
  d.filters = {{"Group", Op::Eq, S("G1")}};
  r = BuildSchedule(d, recs);
  d8test::Check(r.rows.back().kind == Row::GrandTotal && r.rows.back().cells[1] == "zero grand", "zero text: the grand total has its own text");
  ScheduleDef def2;
  Field p = P("Qty", 0);
  def2.fields = {p};
  d8test::Check(BuildSchedule(def2, recs).rows[1].cells[0] == "-", "not evaluated: the default text is a dash");
}

// D8-ARCHICAD-SCHED-0035: grand totals add displayed rounded values or exact values. Three rows of 0.333 shown with two decimals:
// each row "0.33"; rounded mode total 3 x 0.33 = 0.99; exact mode total 0.999 -> "1.00".
D8_EXTRA_TEST(Rw3DocScheduleRoundedOrExactGrandTotal) {
  std::vector<Record> recs = {Rec({{"V", N(0.333)}}), Rec({{"V", N(0.333)}}), Rec({{"V", N(0.333)}})};
  ScheduleDef d;
  Field f = P("V", 2); f.aggregate = Aggregate::Total;
  d.fields = {f};
  d.grandTotal = true;
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(r.rows[0].cells[0] == "0.33" && r.rows[3].cells[0] == "1.00", "grand total, exact mode: rows 0.33, total 1.00");
  d.roundedGrandTotals = true;
  r = BuildSchedule(d, recs);
  d8test::Check(r.rows[3].cells[0] == "0.99", "grand total, rounded mode: total 0.99");
}

// D8-REVIT-ANNOT-0046: revisions with numbering, issued status and its lock. Five revisions: Numeric, Numeric, Alphanumeric,
// Alphanumeric, Alphanumeric -> numbers 1, 2, A, B, C. Numbering None gives blank. After rev 2 becomes Alphanumeric: 1, A, B, C, D.
// Issuing rev 1 records issued-to and issued-by and locks it: description edit, numbering change and a new cloud are refused; unissuing
// reopens it. Issuing twice is refused.
D8_EXTRA_TEST(Rw3DocRevisionNumberingAndIssuedLock) {
  RevisionList l;
  RevNumbering kinds[] = {RevNumbering::Numeric, RevNumbering::Numeric, RevNumbering::Alphanumeric, RevNumbering::Alphanumeric, RevNumbering::Alphanumeric};
  for (int i = 0; i < 5; ++i) l.Add(kinds[i], "2026-0" + std::to_string(i + 1) + "-01", "rev " + std::to_string(i + 1));
  auto numbers = [&] { std::string s; for (auto& r : l.All()) s += r.number + ","; return s; };
  d8test::Check(numbers() == "1,2,A,B,C,", "revisions: numeric 1 2 then alphanumeric A B C");
  l.Add(RevNumbering::None, "2026-06-01", "rev 6");
  d8test::Check(numbers() == "1,2,A,B,C,,", "revisions: numbering None is blank");
  d8test::Check(l.SetNumbering(2, RevNumbering::Alphanumeric) && numbers() == "1,A,B,C,D,,", "revisions: changing rev 2 to alphanumeric renumbers: 1 A B C D");
  d8test::Check(l.Issue(1, "Contractor", "Architect") && l.All()[0].issued && l.All()[0].issuedTo == "Contractor" && l.All()[0].issuedBy == "Architect", "revisions: issue records issued-to and issued-by");
  d8test::Check(!l.EditDescription(1, "changed") && l.All()[0].description == "rev 1", "revisions: an issued revision's description is locked");
  d8test::Check(!l.SetNumbering(1, RevNumbering::None) && l.All()[0].numbering == RevNumbering::Numeric, "revisions: an issued revision's numbering is locked");
  d8test::Check(!l.AddCloud("c1", "A101", 1), "revisions: no cloud can join an issued revision");
  d8test::Check(!l.Issue(1, "x", "y"), "revisions: issuing twice is refused");
  d8test::Check(l.EditDescription(2, "changed") && l.All()[1].description == "changed", "revisions: an unissued revision stays editable");
  d8test::Check(l.Unissue(1) && l.EditDescription(1, "reopened") && l.AddCloud("c1", "A101", 1), "revisions: unissuing reopens it");
}

// D8-REVIT-ANNOT-0045 (and D8-REVIT-VIEW-0021): the revision schedule of a sheet lists the revisions whose clouds are on it, in sequence,
// with the cloud ids. Revisions 1 (numeric 1), 2 (numeric 2), 3 (alphanumeric A), 4 (numeric 3). Clouds: c1, c2 (rev 1, A101), c3 (rev 2,
// A102), c4 (rev 3, A101), c5 (rev 4, A101). Sheet A101: rows "1" {c1,c2}, "A" {c4}, "3" {c5}; sheet A102: "2" {c3}; sheet A103: none.
// Setting revision 3's show to None removes it from the A101 schedule.
D8_EXTRA_TEST(Rw3DocRevisionScheduleListsCloudsPerSheet) {
  RevisionList l;
  l.Add(RevNumbering::Numeric, "2026-01-01", "first");
  l.Add(RevNumbering::Numeric, "2026-02-01", "second");
  l.Add(RevNumbering::Alphanumeric, "2026-03-01", "third");
  l.Add(RevNumbering::Numeric, "2026-04-01", "fourth");
  l.AddCloud("c1", "A101", 1); l.AddCloud("c2", "A101", 1); l.AddCloud("c3", "A102", 2); l.AddCloud("c4", "A101", 3); l.AddCloud("c5", "A101", 4);
  auto rows = l.ScheduleOnSheet("A101");
  d8test::Check(rows.size() == 3 && rows[0].number == "1" && rows[0].clouds == std::vector<std::string>({"c1", "c2"}) && rows[1].number == "A" && rows[1].clouds == std::vector<std::string>({"c4"}) &&
                    rows[2].number == "3" && rows[2].clouds == std::vector<std::string>({"c5"}) && rows[2].date == "2026-04-01" && rows[2].description == "fourth",
                "revision schedule: A101 lists revisions 1, A, 3 with their clouds");
  rows = l.ScheduleOnSheet("A102");
  d8test::Check(rows.size() == 1 && rows[0].number == "2" && rows[0].clouds == std::vector<std::string>({"c3"}), "revision schedule: A102 lists revision 2");
  d8test::Check(l.ScheduleOnSheet("A103").empty(), "revision schedule: a sheet without clouds lists nothing");
  l.SetShow(3, RevShow::None);
  rows = l.ScheduleOnSheet("A101");
  d8test::Check(rows.size() == 2 && rows[0].number == "1" && rows[1].number == "3", "revision schedule: show None removes the revision");
}

// D8-REVIT-WSHARE-0003: exclusive borrowing, requests routed to the owner, outdated elements. Elements W1, W2; users alice and bob.
// alice edits W1 (borrows it). bob editing W1 is refused with code "reserved" naming alice; bob editing W2 succeeds. bob's request for W1
// is addressed to alice. alice synchronizes: W1 reaches revision 2 and is free. bob's local copy is at revision 1, so editing W1 is
// refused as outdated until bob receives; after receiving, the edit succeeds and bob owns W1.
D8_EXTRA_TEST(Rw3DocWorksharingExclusiveBorrowAndOutdated) {
  TeamProject p;
  p.AddElement("W1"); p.AddElement("W2");
  p.Join("alice"); p.Join("bob");
  d8test::Check(p.Edit("alice", "W1").ok && p.OwnerOf("W1") == "alice", "borrow: editing borrows the element");
  TeamResult r = p.Edit("bob", "W1");
  d8test::Check(!r.ok && r.code == "reserved" && r.owner == "alice" && r.message.find("alice") != std::string::npos, "borrow: a second user is refused with a message naming the owner");
  d8test::Check(p.Edit("bob", "W2").ok && p.OwnerOf("W2") == "bob", "borrow: another element stays editable");
  int req = p.Request("bob", "W1");
  d8test::Check(req > 0 && p.Requests().size() == 1 && p.Requests()[0].owner == "alice" && p.Requests()[0].requester == "bob", "borrow: the request is routed to the owner");
  d8test::Check(p.Request("bob", "W2") == 0 && p.Request("alice", "W1") == 0, "borrow: no request for an element one owns");
  p.SyncWithCentral("alice");
  d8test::Check(p.ServerRev("W1") == 2 && p.OwnerOf("W1").empty() && p.LocalRev("alice", "W1") == 2, "borrow: sync publishes revision 2 and releases the element");
  r = p.Edit("bob", "W1");
  d8test::Check(!r.ok && r.code == "outdated", "outdated: bob's copy is behind, edit refused");
  std::vector<std::string> got = p.Receive("bob");
  d8test::Check(got == std::vector<std::string>({"W1"}) && p.Receive("bob").empty(), "outdated: receive brings in W1 once");
  d8test::Check(p.Edit("bob", "W1").ok && p.OwnerOf("W1") == "bob", "outdated: after the update bob can borrow W1");
  TeamProject manual(false);
  manual.AddElement("X"); manual.Join("u");
  r = manual.Edit("u", "X");
  d8test::Check(!r.ok && r.code == "not-reserved", "reserve mode: editing without reserving is refused");
  d8test::Check(manual.Reserve("u", "X").ok && manual.Edit("u", "X").ok, "reserve mode: reserve, then edit");
}

// D8-REVIT-COLLAB-0006: relinquish only without unsynchronized changes. alice edits W1 and W2 (owns both, unsent). Relinquish is
// refused with code "unsent-changes" and she still owns both. After Send, relinquish succeeds and she owns zero elements. A reservation
// without edits can be relinquished at once. bob can then borrow W1 after receiving.
D8_EXTRA_TEST(Rw3DocRelinquishRefusedWithLocalChanges) {
  TeamProject p;
  p.AddElement("W1"); p.AddElement("W2"); p.AddElement("W3");
  p.Join("alice"); p.Join("bob");
  p.Edit("alice", "W1"); p.Edit("alice", "W2");
  p.Reserve("alice", "W3");
  TeamResult r = p.Relinquish("alice");
  d8test::Check(!r.ok && r.code == "unsent-changes" && p.OwnedBy("alice").size() == 3, "relinquish: refused with local changes, ownership kept");
  d8test::Check(p.Send("alice") == 2 && p.ServerRev("W1") == 2 && p.ServerRev("W2") == 2 && p.ServerRev("W3") == 1, "relinquish: send publishes the two edited elements");
  r = p.Relinquish("alice");
  d8test::Check(r.ok && p.OwnedBy("alice").empty(), "relinquish: after the send alice owns zero elements");
  d8test::Check(p.Edit("bob", "W1").code == "outdated" && !p.Receive("bob").empty() && p.Edit("bob", "W1").ok, "relinquish: bob can borrow W1 after receiving");
  TeamProject q;
  q.AddElement("E"); q.Join("u");
  q.Reserve("u", "E");
  d8test::Check(q.Relinquish("u").ok && q.OwnerOf("E").empty(), "relinquish: a bare reservation is handed back");
}

// D8-REVIT-COLLAB-0020: compare two versions: added, modified, removed. Older: a{H=3,M=brick} b{H=3} c{H=2} d{H=1} e{H=4}.
// Newer: a{H=3,M=brick} (unchanged) b{H=3.5} (modified H) c{H=2,M=steel} (modified: M added) e{H=4} (unchanged) f{H=1} g{H=9} (added).
// d is removed. Result: added f, g; modified b (H), c (M); removed d; a and e unreported. A property that disappears is a modification.
D8_EXTRA_TEST(Rw3DocModelCompareAddedModifiedRemoved) {
  auto el = [](const char* id, std::map<std::string, std::string> p) { ModelElement e; e.id = id; e.props = p; return e; };
  std::vector<ModelElement> older = {el("a", {{"H", "3"}, {"M", "brick"}}), el("b", {{"H", "3"}}), el("c", {{"H", "2"}}), el("d", {{"H", "1"}}), el("e", {{"H", "4"}})};
  std::vector<ModelElement> newer = {el("g", {{"H", "9"}}), el("a", {{"H", "3"}, {"M", "brick"}}), el("b", {{"H", "3.5"}}), el("c", {{"H", "2"}, {"M", "steel"}}), el("e", {{"H", "4"}}), el("f", {{"H", "1"}})};
  ModelDiff d = DiffModels(older, newer);
  d8test::Check(d.added == std::vector<std::string>({"f", "g"}), "compare: added f, g");
  d8test::Check(d.modified == std::vector<std::string>({"b", "c"}) && d.changedProps["b"] == std::vector<std::string>({"H"}) && d.changedProps["c"] == std::vector<std::string>({"M"}), "compare: modified b (H) and c (M)");
  d8test::Check(d.removed == std::vector<std::string>({"d"}), "compare: removed d");
  newer[3].props.erase("M");
  newer[3].props["H"] = "2";
  older[2].props["M"] = "steel";
  d = DiffModels(older, newer);
  d8test::Check(d.modified == std::vector<std::string>({"b", "c"}) && d.changedProps["c"] == std::vector<std::string>({"M"}), "compare: a property that disappears is a modification");
  ModelDiff same = DiffModels(older, older);
  d8test::Check(same.added.empty() && same.modified.empty() && same.removed.empty(), "compare: a version against itself has no changes");
}

// D8-ARCHICAD-TEAM-0005: request, grant, decline. B edits wall1 (unsent). A requests wall1: the request names owner B. B declines:
// ownership stays with B. A requests again, B grants: the reservation moves to A at once, B's unsent edit is sent first (server revision
// 2), so A must receive before editing: edit refused as outdated, then ok. Only the owner can grant. C and A both request wall2 owned by
// B; B releases (after sending): the earliest requester A takes it, C's request stays pending.
D8_EXTRA_TEST(Rw3DocTeamworkRequestGrantDecline) {
  TeamProject p(false);
  p.AddElement("wall1"); p.AddElement("wall2");
  p.Join("A"); p.Join("B"); p.Join("C");
  p.Reserve("B", "wall1"); p.Edit("B", "wall1");
  p.Reserve("B", "wall2");
  int r1 = p.Request("A", "wall1");
  d8test::Check(r1 > 0 && p.Requests()[0].owner == "B", "request: addressed to the owner");
  d8test::Check(!p.Grant("A", r1).ok && p.OwnerOf("wall1") == "B", "request: only the owner can grant");
  d8test::Check(p.Decline("B", r1).ok && p.OwnerOf("wall1") == "B" && p.Requests().empty(), "request: declining leaves ownership unchanged");
  int r2 = p.Request("A", "wall1");
  d8test::Check(p.Grant("B", r2).ok && p.OwnerOf("wall1") == "A" && p.Requests().empty(), "request: a grant reserves the element for the requester at once");
  d8test::Check(p.ServerRev("wall1") == 2, "request: the owner's unsent edit was sent by the grant");
  TeamResult e = p.Edit("A", "wall1");
  d8test::Check(!e.ok && e.code == "outdated", "request: A must receive before editing");
  p.Receive("A");
  d8test::Check(p.Edit("A", "wall1").ok, "request: after receiving A can edit");
  d8test::Check(!p.Edit("B", "wall1").ok && p.Edit("B", "wall1").owner == "A", "request: the previous owner is now refused");
  p.Request("C", "wall2");
  p.Request("A", "wall2");
  d8test::Check(p.Release("B", "wall2").ok && p.OwnerOf("wall2") == "C" && p.Requests().size() == 1 && p.Requests()[0].requester == "A", "request: a release grants the earliest pending request");
  p.Edit("C", "wall2");
  d8test::Check(!p.Release("C", "wall2").ok && p.Release("C", "wall2").code == "unsent-changes", "request: release needs the changes sent first");
}

// D8-ARCHICAD-TEAM-0044: reservation results per region. Region R1 holds e1..e10; u2 owns e1, e2 and u3 owns e3. Region R2 holds f1..f4,
// free. u1 reserves all 14: R1 7 reserved, 3 blocked (e1, e2, e3); R2 4 reserved, 0 blocked. Requesting the blocked ones creates one
// message per owner: u2 {e1, e2}, u3 {e3}. An element changed on the server since u1's copy is blocked as well (receive needed).
D8_EXTRA_TEST(Rw3DocTeamworkReservationResultsPerRegion) {
  TeamProject p(false);
  std::vector<std::string> all;
  for (int i = 1; i <= 10; ++i) { std::string id = "e" + std::to_string(i); p.AddElement(id, "R1"); all.push_back(id); }
  for (int i = 1; i <= 4; ++i) { std::string id = "f" + std::to_string(i); p.AddElement(id, "R2"); all.push_back(id); }
  p.Join("u1"); p.Join("u2"); p.Join("u3");
  p.Reserve("u2", "e1"); p.Reserve("u2", "e2"); p.Reserve("u3", "e3");
  auto res = p.ReserveMany("u1", all);
  d8test::Check(res["R1"].reserved == 7 && res["R1"].blocked == 3 && res["R1"].blockedIds == std::vector<std::string>({"e1", "e2", "e3"}), "region reserve: R1 7 reserved, 3 blocked (e1 e2 e3)");
  d8test::Check(res["R2"].reserved == 4 && res["R2"].blocked == 0, "region reserve: R2 4 reserved");
  d8test::Check(p.OwnedBy("u1").size() == 11 && p.OwnerOf("e1") == "u2", "region reserve: u1 owns 11, blocked ones keep their owner");
  auto msgs = p.RequestBlocked("u1", res["R1"].blockedIds);
  d8test::Check(msgs.size() == 2 && msgs["u2"] == std::vector<std::string>({"e1", "e2"}) && msgs["u3"] == std::vector<std::string>({"e3"}) && p.Requests().size() == 3, "region reserve: one message per owner, three requests");
  // server change makes an element need a receive
  TeamProject q(false);
  q.AddElement("x", "R"); q.AddElement("y", "R");
  q.Join("a"); q.Join("b");
  q.Reserve("a", "x"); q.Edit("a", "x"); q.Send("a"); q.Release("a", "x");
  auto res2 = q.ReserveMany("b", {"x", "y"});
  d8test::Check(res2["R"].reserved == 1 && res2["R"].blocked == 1 && res2["R"].blockedIds[0] == "x" && q.Reserve("b", "x").code == "need-receive", "region reserve: a stale element is blocked until received");
}

}  // namespace
