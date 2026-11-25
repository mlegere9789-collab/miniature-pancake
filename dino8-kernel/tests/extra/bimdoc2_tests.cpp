// Second batch of tests of include/dino8/kernel/bimdoc_calc.h: sheet filter and min / max, phases, global-parameter filter, multi-category
// schedules, sheet lists, embedded schedules, keynotes (file, legend, XML database), browser organisation, zones, component order,
// selection-only listing, snapshots, forced leave, 3D compare, auto-reserve, attribute reservation, issues, worksets, model updates.
// One test per Register row (or per pair of rows that share a scenario). Every expected value is worked out by hand in the comment of
// the test. The vendor pages could not be fetched from this environment, so scenarios come from the rows' acceptance sketches; what the
// rows do not state is listed in the header of bimdoc_calc.h and in each row's notes. Selection, undo, UI, cloud service and file-format
// clauses are not tested.
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/bimdoc_calc.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::bimdoc;

Field P2(const std::string& n, int decimals = -1) { Field f; f.name = n; f.decimals = decimals; return f; }
Value S2(const char* s) { return Value::Of(s); }
Value N2(double d) { return Value::Of(d); }
Record Rec2(std::initializer_list<std::pair<const char*, Value>> kv) {
  Record r;
  for (auto& p : kv) r[p.first] = p.second;
  return r;
}

// D8-REVIT-SCHED-0004 (sheet filter clause) and D8-REVIT-SCHED-0020 (minimum and maximum clause).
// Elements e1 {sheets A101;A102, L 1.2}, e2 {A101, 2.3}, e3 {A102, 0.5}, e4 {no sheet, 4.0}. Placed on A101 the schedule lists e1, e2;
// on A102 e1, e3 (an element in views of two sheets is listed on both); on A103 nothing; with the option off all four. Combined with
// Id != e1 on A101: e2. Minimum and maximum of L: on A101 min 1.2 max 2.3 -> "1.2 - 2.3"; over all four min 0.5 max 4.0 -> "0.5 - 4.0"
// (the "min - max" display format is an assumption, the two values are the checked quantity).
D8_EXTRA_TEST(Rw3Doc2ScheduleSheetFilterAndMinMax) {
  std::vector<Record> recs = {Rec2({{"Id", S2("e1")}, {"Sheets", S2("A101;A102")}, {"L", N2(1.2)}}), Rec2({{"Id", S2("e2")}, {"Sheets", S2("A101")}, {"L", N2(2.3)}}),
                              Rec2({{"Id", S2("e3")}, {"Sheets", S2("A102")}, {"L", N2(0.5)}}), Rec2({{"Id", S2("e4")}, {"L", N2(4.0)}})};
  ScheduleDef d;
  Field l = P2("L", 1); l.aggregate = Aggregate::MinMax;
  d.fields = {P2("Id"), l};
  d.grandTotal = true;
  auto ids = [&](const ScheduleDef& def) { std::string s; for (auto& r : BuildSchedule(def, recs).rows) if (r.kind == Row::Data) s += r.cells[0]; return s; };
  d.filterBySheet = true; d.sheet = "A101";
  d8test::Check(ids(d) == "e1e2", "sheet filter: A101 lists e1 e2");
  d8test::Check(BuildSchedule(d, recs).rows.back().cells[1] == "1.2 - 2.3", "min and max: 1.2 - 2.3 on A101");
  d.sheet = "A102";
  d8test::Check(ids(d) == "e1e3", "sheet filter: A102 lists e1 e3");
  d.sheet = "A103";
  d8test::Check(ids(d).empty(), "sheet filter: A103 lists nothing");
  d.sheet = "A101";
  d.filters = {{"Id", Op::Ne, S2("e1")}};
  d8test::Check(ids(d) == "e2", "sheet filter: combined with another condition");
  d.filters.clear(); d.filterBySheet = false;
  d8test::Check(ids(d) == "e1e2e3e4" && BuildSchedule(d, recs).rows.back().cells[1] == "0.5 - 4.0", "sheet filter off: all four, min 0.5 max 4.0");
}

// D8-REVIT-SCHED-0019: Phase and Phase Filter. Phases P1 P2 P3. Elements (created, demolished): a (P1,-) b (P1,P2) c (P2,-) d (P2,P2)
// e (P3,-) f (P1,P3) g (P2,P3). Seen from P2: a Existing, b Demolished, c New, d Temporary, e absent, f Existing, g New.
// Show Complete (New, Existing, Temporary) lists a c d f g, so the demolished b is excluded; Show New: c g; Show Demo: b;
// Previous + Demo: a b f. Seen from P3: a Existing, b and d absent (gone before), c Existing, e New, f Demolished, g Demolished;
// Show Complete lists a c e. The contents of the presets are from recollection of the product and need an owner check.
D8_EXTRA_TEST(Rw3Doc2SchedulePhaseFilter) {
  std::vector<std::string> order = {"P1", "P2", "P3"};
  struct E { const char* id; const char* c; const char* d; };
  E es[] = {{"a", "P1", ""}, {"b", "P1", "P2"}, {"c", "P2", ""}, {"d", "P2", "P2"}, {"e", "P3", ""}, {"f", "P1", "P3"}, {"g", "P2", "P3"}};
  std::vector<Record> recs;
  for (auto& e : es) recs.push_back(Rec2({{"Id", S2(e.id)}, {"PhaseCreated", S2(e.c)}, {"PhaseDemolished", S2(e.d)}}));
  auto ids = [&](const char* phase, const char* preset) {
    std::string s;
    for (auto& r : FilterByPhase(recs, order, phase, PhasePreset(preset))) s += r["Id"].s;
    return s;
  };
  d8test::Check(ids("P2", "Show Complete") == "acdfg", "phase: Show Complete from P2 excludes the demolished b");
  d8test::Check(ids("P2", "Show New") == "cg" && ids("P2", "Show Demo") == "b" && ids("P2", "Show Previous + Demo") == "abf", "phase: New, Demo and Previous + Demo from P2");
  d8test::Check(ids("P2", "Show All") == "abcdfg", "phase: Show All from P2 omits only e (not yet built)");
  d8test::Check(ids("P3", "Show Complete") == "ace" && ids("P3", "Show Demo") == "fg" && ids("P3", "Show New") == "e", "phase: from P3 b and d are gone, f and g demolished");
  PhaseStatus st;
  d8test::Check(StatusInPhase(order, "P2", "P2", "P2", st) && st == PhaseStatus::Temporary && !StatusInPhase(order, "P1", "", "PX", st), "phase: temporary status, unknown phase refused");
  auto stat = FilterByPhase({recs[1]}, order, "P2", PhasePreset("Show All"));
  d8test::Check(stat.size() == 1 && stat[0]["PhaseStatus"].s == "Demolished", "phase: the status is added to the record");
}

// D8-REVIT-VCTRL-0036: merge a phase into the next or previous one. Phases P1 P2 P3 P4; elements x (P1,-) y (P2,P3) z (P3,P3) w (P2,-)
// u (P4,-). Merging P3 into the next: y (P2,P4), z (P4,P4), phases P1 P2 P4, and no element mentions P3. Merging P1 into the previous
// and P4 into the next are refused (no neighbour). Merging P2 into the previous then gives P1 P4, y (P1,P4), w (P1,-). Five elements remain.
D8_EXTRA_TEST(Rw3Doc2MergePhase) {
  PhaseModel m;
  m.order = {"P1", "P2", "P3", "P4"};
  m.elements = {{"x", "P1", ""}, {"y", "P2", "P3"}, {"z", "P3", "P3"}, {"w", "P2", ""}, {"u", "P4", ""}};
  d8test::Check(MergePhase(m, "P3", true) && m.order == std::vector<std::string>({"P1", "P2", "P4"}), "merge phase: P3 into next leaves P1 P2 P4");
  d8test::Check(m.elements[1].created == "P2" && m.elements[1].demolished == "P4" && m.elements[2].created == "P4" && m.elements[2].demolished == "P4", "merge phase: y and z re-pointed to P4");
  bool refs = false;
  for (auto& e : m.elements) if (e.created == "P3" || e.demolished == "P3") refs = true;
  d8test::Check(!refs, "merge phase: no element still references P3");
  d8test::Check(!MergePhase(m, "P1", false) && !MergePhase(m, "P4", true) && !MergePhase(m, "PX", true) && m.order.size() == 3, "merge phase: no previous / next / unknown phase is refused");
  d8test::Check(MergePhase(m, "P2", false) && m.order == std::vector<std::string>({"P1", "P4"}) && m.elements[1].created == "P1" && m.elements[3].created == "P1" && m.elements.size() == 5, "merge phase: P2 into previous");
}

// D8-REVIT-SCHED-0026: filter by whether a parameter is tied to a chosen global parameter. Doors 1..5 with the Width parameter tied to
// global GW for doors 1 and 3, to GX for door 4, untied for 2 and 5. Associated with GW: 1, 3. Not associated with GW: 2, 4, 5.
// Associated with GX: 4. Associated with a global that does not exist: none.
D8_EXTRA_TEST(Rw3Doc2ScheduleGlobalParameterFilter) {
  std::vector<Record> recs;
  const char* g[] = {"GW", "", "GW", "GX", ""};
  for (int i = 0; i < 5; ++i) {
    Record r = Rec2({{"Id", N2(i + 1)}, {"Width", N2(0.9)}});
    if (*g[i]) r["Width@global"] = S2(g[i]);
    recs.push_back(r);
  }
  ScheduleDef d;
  d.fields = {P2("Id", 0)};
  auto ids = [&](Op op, const char* glob) {
    d.filters = {{"Width", op, S2(glob)}};
    std::string s;
    for (auto& r : BuildSchedule(d, recs).rows) s += r.cells[0];
    return s;
  };
  d8test::Check(ids(Op::AssocGlobal, "GW") == "13", "global filter: associated with GW -> 1 3");
  d8test::Check(ids(Op::NotAssocGlobal, "GW") == "245", "global filter: not associated with GW -> 2 4 5");
  d8test::Check(ids(Op::AssocGlobal, "GX") == "4" && ids(Op::AssocGlobal, "GZ").empty(), "global filter: GX -> 4, a missing global -> none");
}

// D8-REVIT-SCHED-0025 and D8-REVIT-SCHED-0015: multi-category schedule and shared-parameter dropout.
// Doors (Mark, FireRating, Area): 1.89, 2.1, 1.6 -> 5.59. Windows (Mark, FireRating, Area): 1.2, 0.9 -> 2.1. Walls (Mark, Area, no
// FireRating): 30, 20 -> 50. Requesting Mark and Area lists all seven rows; per-category footers 5.59, 2.1, 50 and the grand total
// 57.69 (their sum). Requesting Mark and FireRating drops Walls: five rows, dropped = Walls.
D8_EXTRA_TEST(Rw3Doc2MultiCategoryScheduleAndSharedParameterDropout) {
  auto item = [](const char* m, double a) { return Rec2({{"Mark", S2(m)}, {"Area", N2(a)}}); };
  std::vector<CategoryData> cats(3);
  cats[0].name = "Doors"; cats[0].fields = {"Mark", "FireRating", "Area"}; cats[0].items = {item("D1", 1.89), item("D2", 2.1), item("D3", 1.6)};
  cats[1].name = "Windows"; cats[1].fields = {"Mark", "FireRating", "Area"}; cats[1].items = {item("W1", 1.2), item("W2", 0.9)};
  cats[2].name = "Walls"; cats[2].fields = {"Mark", "Area"}; cats[2].items = {item("A", 30), item("B", 20)};
  std::vector<std::string> dropped;
  std::vector<Record> recs = MultiCategoryRecords(cats, {"Mark", "Area"}, &dropped);
  d8test::Check(recs.size() == 7 && dropped.empty(), "multi-category: seven rows from three categories");
  ScheduleDef d;
  Field a = P2("Area", 2); a.aggregate = Aggregate::Total;
  d.fields = {P2("Category"), P2("Mark"), a};
  SortKey k; k.field = "Category"; k.footer = true;
  d.sort = {k};
  d.grandTotal = true;
  ScheduleResult r = BuildSchedule(d, recs);
  std::vector<std::string> foot;
  for (auto& x : r.rows) if (x.kind == Row::GroupFooter) foot.push_back(x.label + "=" + x.cells[2]);
  d8test::Check(foot == std::vector<std::string>({"Doors=5.59", "Walls=50.00", "Windows=2.10"}) && r.rows.back().cells[2] == "57.69", "multi-category: footers 5.59, 50.00, 2.10 and grand total 57.69");
  dropped.clear();
  recs = MultiCategoryRecords(cats, {"Mark", "FireRating"}, &dropped);
  d8test::Check(recs.size() == 5 && dropped == std::vector<std::string>({"Walls"}), "shared parameter: a category lacking FireRating drops out");
}

// D8-REVIT-SCHED-0009: sheet list. Sheets A101 Plans, A102 Elevations, A103 Details (drawn by KS), A201 Sections, X001 Reserved
// (placeholder). With placeholders: five rows sorted by number A101 A102 A103 A201 X001; without: four. Sorted by name: Details,
// Elevations, Plans, Reserved, Sections. The user-defined field DrawnBy shows KS for A103. Filter Placeholder = No lists four.
D8_EXTRA_TEST(Rw3Doc2SheetList) {
  std::vector<SheetInfo> sheets(5);
  sheets[0].number = "A102"; sheets[0].name = "Elevations";
  sheets[1].number = "A101"; sheets[1].name = "Plans";
  sheets[2].number = "X001"; sheets[2].name = "Reserved"; sheets[2].placeholder = true;
  sheets[3].number = "A201"; sheets[3].name = "Sections";
  sheets[4].number = "A103"; sheets[4].name = "Details"; sheets[4].user["DrawnBy"] = "KS";
  ScheduleDef d;
  d.fields = {P2("Number"), P2("Name"), P2("DrawnBy")};
  SortKey byNumber; byNumber.field = "Number";
  d.sort = {byNumber};
  auto col = [&](const ScheduleDef& def, bool placeholders, int c) { std::string s; for (auto& r : BuildSchedule(def, SheetRecords(sheets, placeholders)).rows) s += r.cells[c] + ","; return s; };
  d8test::Check(col(d, true, 0) == "A101,A102,A103,A201,X001,", "sheet list: five sheets sorted by number");
  d8test::Check(col(d, false, 0) == "A101,A102,A103,A201,", "sheet list: without placeholders four");
  d.filters = {{"Placeholder", Op::Eq, S2("No")}};
  d8test::Check(col(d, true, 0) == "A101,A102,A103,A201,", "sheet list: a Placeholder = No filter does the same");
  d.filters.clear();
  d.sort[0].field = "Name";
  d8test::Check(col(d, true, 1) == "Details,Elevations,Plans,Reserved,Sections,", "sheet list: sorted by name");
  d.sort[0].field = "Number";
  ScheduleResult r = BuildSchedule(d, SheetRecords(sheets, true));
  d8test::Check(r.rows[2].cells[2] == "KS" && r.rows[0].cells[2] == "-", "sheet list: user-defined field value, unset shows not-evaluated");
}

// D8-REVIT-SCHED-0014: embedded schedule. Rooms R101, R102, R103; doors d1 and d2 open into R101, d3 into R103, d4 into R999 (no room
// row). Rows: R101, d1, d2, R102, R103, d3 with levels 0 1 1 0 0 1. Only rooms, spaces, duct systems, piping systems and circuits can embed.
D8_EXTRA_TEST(Rw3Doc2EmbeddedMemberSchedule) {
  std::vector<Record> rooms = {Rec2({{"Number", S2("R101")}}), Rec2({{"Number", S2("R102")}}), Rec2({{"Number", S2("R103")}})};
  std::vector<Record> doors = {Rec2({{"Mark", S2("d1")}, {"Room", S2("R101")}}), Rec2({{"Mark", S2("d2")}, {"Room", S2("R101")}}), Rec2({{"Mark", S2("d3")}, {"Room", S2("R103")}}),
                               Rec2({{"Mark", S2("d4")}, {"Room", S2("R999")}})};
  ScheduleDef pd, md;
  pd.fields = {P2("Number")};
  md.fields = {P2("Mark")};
  EmbeddedResult r = BuildEmbedded("Rooms", pd, rooms, "Number", md, doors, "Room");
  std::string cells, levels;
  for (auto& x : r.rows) { cells += x.cells[0] + ","; levels += std::to_string(x.level); }
  d8test::Check(r.ok && cells == "R101,d1,d2,R102,R103,d3," && levels == "011001", "embedded: members listed beneath their parent, orphan member omitted");
  d8test::Check(!BuildEmbedded("Walls", pd, rooms, "Number", md, doors, "Room").ok, "embedded: walls cannot embed a schedule");
  d8test::Check(EmbeddedScheduleAllowed("Spaces") && EmbeddedScheduleAllowed("Duct Systems") && EmbeddedScheduleAllowed("Piping Systems") && EmbeddedScheduleAllowed("Electrical Circuits") && !EmbeddedScheduleAllowed("Doors"),
                "embedded: the five documented categories");
}

const char* kKeynoteFile =
    "A\tSitework\n"
    "A10\tFoundations\tA\n"
    "A1010\tStandard foundations\tA10\n"
    "\n"
    "B\tShell\n"
    "B10\tSuperstructure\tB\n"
    "B1010\tFloor construction\tB10\n"
    "B1020\tRoof construction\tB10\n";

// D8-REVIT-SCHED-0023: keynote file. The file above has 7 keynotes (the blank line is skipped). B1020 reads "Roof construction"; its
// division is B; A is its own division; the children of B10 are B1010 and B1020. Windows line ends parse the same. A duplicate key,
// a missing description, an empty key and an unknown parent are errors naming the line (or the parent).
D8_EXTRA_TEST(Rw3Doc2KeynoteFileParse) {
  KeynoteTable t = ParseKeynoteFile(kKeynoteFile);
  d8test::Check(t.ok && t.notes.size() == 7, "keynote file: seven keynotes");
  const Keynote* k = t.Find("B1020");
  d8test::Check(k && k->text == "Roof construction" && k->parent == "B10", "keynote file: the key resolves to its description and parent");
  d8test::Check(t.Division("B1020") == "B" && t.Division("A") == "A" && t.Division("A1010") == "A", "keynote file: divisions");
  d8test::Check(t.Children("B10") == std::vector<std::string>({"B1010", "B1020"}) && t.Children("B1010").empty(), "keynote file: children");
  KeynoteTable crlf = ParseKeynoteFile("X\tOne\r\nY\tTwo\tX\r\n");
  d8test::Check(crlf.ok && crlf.notes.size() == 2 && crlf.notes[1].text == "Two" && crlf.notes[1].parent == "X", "keynote file: CRLF line ends");
  KeynoteTable bad = ParseKeynoteFile("A\tOne\nA\tTwo\n");
  d8test::Check(!bad.ok && bad.error.find("line 2") != std::string::npos && bad.error.find("duplicate") != std::string::npos, "keynote file: duplicate key names line 2");
  d8test::Check(!ParseKeynoteFile("A\n").ok && !ParseKeynoteFile("\tText\n").ok, "keynote file: missing description or key");
  bad = ParseKeynoteFile("A\tOne\tZ\n");
  d8test::Check(!bad.ok && bad.error.find("unknown parent Z") != std::string::npos, "keynote file: unknown parent");
}

// D8-REVIT-SCHED-0012: keynote legend filters. Keys placed in views of the sheet: A1010, B1020. Limited to the sheet: A1010 B1020
// (file order). Division B only: B B10 B1010 B1020. Both: B1020. No filter: all seven.
D8_EXTRA_TEST(Rw3Doc2KeynoteLegendFilters) {
  KeynoteTable t = ParseKeynoteFile(kKeynoteFile);
  auto keys = [&](const LegendOptions& o) { std::string s; for (auto& n : KeynoteLegend(t, o)) s += n.key + ","; return s; };
  LegendOptions o;
  d8test::Check(KeynoteLegend(t, o).size() == 7, "legend: no filter lists all seven");
  o.limitToSheet = true; o.keysOnSheet = {"A1010", "B1020", "Z9"};
  d8test::Check(keys(o) == "A1010,B1020,", "legend: limited to the keys placed on the sheet (a key not in the table is not listed)");
  o.limitToSheet = false; o.division = "B";
  d8test::Check(keys(o) == "B,B10,B1010,B1020,", "legend: division B");
  o.limitToSheet = true;
  d8test::Check(keys(o) == "B1020,", "legend: sheet and division together");
}

// D8-ARCHICAD-NAV-0049: keynote database as XML. Database of four keynotes, one with & < > " in its text. Export then import with Replace
// into an empty database gives an equal database (every field). Exporting folder B10 gives B10, B1010, B1020 (3). Merging a file holding
// existing key K1 (new text) and new key K9: Skip -> added 1, skipped 1, K1 text unchanged; Overwrite -> overwritten 1, K1 text changed;
// Append -> appended under K1-1, and a second append of the same file gives K1-2. Malformed input is refused and changes nothing.
D8_EXTRA_TEST(Rw3Doc2KeynoteDatabaseXml) {
  std::vector<Keynote> db = {{"K1", "Fire & smoke <30 min> \"door\"", "", "ref1"}, {"B10", "Superstructure", "", ""}, {"B1010", "Floor", "B10", "r2"}, {"B1020", "Roof", "B10", "r3"}};
  std::string xml = ExportKeynotesXml(db);
  std::vector<Keynote> back;
  d8test::Check(ImportKeynotes(back, xml, ImportMode::Replace, ImportConflict::Skip) && back.size() == 4, "keynote xml: replace import gives four");
  bool equal = back.size() == db.size();
  for (size_t i = 0; equal && i < db.size(); ++i) equal = back[i].key == db[i].key && back[i].text == db[i].text && back[i].parent == db[i].parent && back[i].ref == db[i].ref;
  d8test::Check(equal, "keynote xml: the imported database equals the exported one, special characters included");
  std::vector<Keynote> sub;
  ImportKeynotes(sub, ExportKeynotesXml(db, "B10"), ImportMode::Replace, ImportConflict::Skip);
  d8test::Check(sub.size() == 3 && sub[0].key == "B10" && sub[1].key == "B1010" && sub[2].key == "B1020", "keynote xml: exporting one folder exports its subtree");
  std::vector<Keynote> in = {{"K1", "new text", "", ""}, {"K9", "fresh", "", ""}};
  std::string inXml = ExportKeynotesXml(in);
  std::vector<Keynote> w = db;
  ImportCounts c;
  d8test::Check(ImportKeynotes(w, inXml, ImportMode::Merge, ImportConflict::Skip, &c) && c.added == 1 && c.skipped == 1 && w.size() == 5 && w[0].text == db[0].text, "keynote xml: merge with skip");
  w = db;
  d8test::Check(ImportKeynotes(w, inXml, ImportMode::Merge, ImportConflict::Overwrite, &c) && c.overwritten == 1 && c.added == 1 && w[0].text == "new text" && w.size() == 5, "keynote xml: merge with overwrite");
  w = db;
  d8test::Check(ImportKeynotes(w, inXml, ImportMode::Merge, ImportConflict::Append, &c) && c.appended == 1 && w.size() == 6 && w[0].text == db[0].text, "keynote xml: merge with append keeps K1");
  bool hasK11 = false;
  for (auto& n : w) if (n.key == "K1-1" && n.text == "new text") hasK11 = true;
  d8test::Check(hasK11, "keynote xml: the appended entry is K1-1");
  ImportKeynotes(w, inXml, ImportMode::Merge, ImportConflict::Append, &c);
  bool hasK12 = false;
  for (auto& n : w) if (n.key == "K1-2") hasK12 = true;
  d8test::Check(hasK12, "keynote xml: a second append is K1-2");
  std::vector<Keynote> keep = db;
  d8test::Check(!ImportKeynotes(keep, "<Keynotes><Keynote key=\"A\"", ImportMode::Replace, ImportConflict::Skip) && keep.size() == 4, "keynote xml: malformed input is refused and the database is unchanged");
}

// D8-REVIT-VIEW-0020: browser organisation by up to six grouping levels. Items (Discipline, Type, Level): v1 (A, Plan, 2), v2 (A, Plan, 1),
// v3 (A, Section, 1), v4 (S, Plan, 1), v5 (no discipline, Plan, 1). Grouped by Discipline then Type, sorted by Level: <none>/Plan/v5,
// A/Plan/v2, A/Plan/v1, A/Section/v3, S/Plan/v4 (v2 before v1 by level). Six levels are accepted, seven refused.
D8_EXTRA_TEST(Rw3Doc2BrowserOrganization) {
  auto it = [](const char* n, const char* d, const char* t, const char* l) {
    BrowserItem b; b.name = n; if (*d) b.props["Discipline"] = d; b.props["Type"] = t; b.props["Level"] = l; return b;
  };
  std::vector<BrowserItem> items = {it("v1", "A", "Plan", "2"), it("v2", "A", "Plan", "1"), it("v3", "A", "Section", "1"), it("v4", "S", "Plan", "1"), it("v5", "", "Plan", "1")};
  BrowserNode root;
  d8test::Check(OrganizeBrowser(items, {"Discipline", "Type"}, "Level", root), "browser: two levels accepted");
  std::vector<std::string> paths;
  for (auto& d : root.children) for (auto& t : d.children) for (auto& n : t.items) paths.push_back(d.label + "/" + t.label + "/" + n);
  d8test::Check(paths == std::vector<std::string>({"<none>/Plan/v5", "A/Plan/v2", "A/Plan/v1", "A/Section/v3", "S/Plan/v4"}), "browser: tree reflects the grouping and sort rules");
  d8test::Check(OrganizeBrowser(items, {"Discipline", "Type", "Level", "Discipline", "Type", "Level"}, "", root), "browser: six levels accepted");
  d8test::Check(!OrganizeBrowser(items, {"Discipline", "Type", "Level", "Discipline", "Type", "Level", "Type"}, "", root), "browser: seven levels refused");
  BrowserNode flat;
  OrganizeBrowser(items, {}, "Level", flat);
  d8test::Check(flat.children.empty() && flat.items == std::vector<std::string>({"v2", "v3", "v4", "v5", "v1"}), "browser: no grouping sorts the items by level then name");
}

// D8-ARCHICAD-SCHED-0021: zone listing. Z1 measured 20 reduced 18, doors 0.9x2.1 and 1.0x2.1, window 1.2x1.5. Z2 12 / 12, door 0.9x2.1.
// Z3 30 / 25, windows 2.0x1.5 and 1.0x1.0. Z1: doors 2, widths 1.9, surface 1.89 + 2.1 = 3.99; windows 1, width 1.2, surface 1.8.
// Z2: doors 1, 0.9, 1.89; windows 0. Z3: doors 0; windows 2, width 3.0, surface 3.0 + 1.0 = 4.0. Column sums: measured 62, reduced 55,
// doors 3, door surface 5.88, windows 3, window surface 5.8: the area sum equals the total of the zones.
D8_EXTRA_TEST(Rw3Doc2ZoneSchedule) {
  std::vector<ZoneInfo> zones = {{"Z1", "Living", 20, 18}, {"Z2", "Hall", 12, 12}, {"Z3", "Studio", 30, 25}};
  std::vector<OpeningInfo> ops = {{"Z1", "Door", 0.9, 2.1}, {"Z1", "Door", 1.0, 2.1}, {"Z1", "Window", 1.2, 1.5}, {"Z2", "Door", 0.9, 2.1}, {"Z3", "Window", 2.0, 1.5}, {"Z3", "Window", 1.0, 1.0}};
  ScheduleDef d;
  auto agg = [](const char* n, int dec) { Field f = P2(n, dec); f.aggregate = Aggregate::Total; return f; };
  d.fields = {P2("Zone"), agg("MeasuredArea", 1), agg("ReducedArea", 1), agg("Doors", 0), agg("DoorWidth", 1), agg("DoorSurface", 2), agg("Windows", 0), agg("WindowWidth", 1), agg("WindowSurface", 2)};
  d.grandTotal = true;
  ScheduleResult r = BuildSchedule(d, ZoneRecords(zones, ops));
  d8test::Check(r.rows[0].cells == std::vector<std::string>({"Z1", "20.0", "18.0", "2", "1.9", "3.99", "1", "1.2", "1.80"}), "zones: Z1 row");
  d8test::Check(r.rows[1].cells == std::vector<std::string>({"Z2", "12.0", "12.0", "1", "0.9", "1.89", "0", "0.0", "0.00"}), "zones: Z2 row");
  d8test::Check(r.rows[2].cells == std::vector<std::string>({"Z3", "30.0", "25.0", "0", "0.0", "0.00", "2", "3.0", "4.00"}), "zones: Z3 row");
  d8test::Check(r.rows[3].kind == Row::GrandTotal && r.rows[3].cells == std::vector<std::string>({"", "62.0", "55.0", "3", "2.8", "5.88", "3", "4.2", "5.80"}), "zones: totals 62, 55, 3 doors, 3 windows, surfaces 5.88 and 5.80");
}

// D8-ARCHICAD-SCHED-0031: components of one element together, in composite order, or purely by the sort field.
// W1 skins in composite order: Brick, Insulation, Gypsum. W2: Concrete, Insulation, Plaster. Sorted by Component.
// Keep together on: elements ordered by their first component (Brick < Concrete): W1 then W2, each in composite order:
// Brick Insulation Gypsum Concrete Insulation Plaster. With W2 starting with Aluminium: W2 first: Aluminium Insulation Plaster Brick
// Insulation Gypsum. Off: sorted across walls by name: Brick Concrete Gypsum Insulation(W1) Insulation(W2) Plaster.
D8_EXTRA_TEST(Rw3Doc2ComponentScheduleKeepTogether) {
  auto comp = [](const char* w, const char* c) { return Rec2({{"Element", S2(w)}, {"Component", S2(c)}}); };
  std::vector<Record> recs = {comp("W1", "Brick"), comp("W1", "Insulation"), comp("W1", "Gypsum"), comp("W2", "Concrete"), comp("W2", "Insulation"), comp("W2", "Plaster")};
  ScheduleDef d;
  d.fields = {P2("Element"), P2("Component")};
  SortKey k; k.field = "Component";
  d.sort = {k};
  auto order = [&](const ScheduleDef& def, const std::vector<Record>& rs) { std::string s; for (auto& r : BuildSchedule(def, rs).rows) s += r.cells[1] + (r.cells[0] == "W1" ? "1" : "2") + " "; return s; };
  d8test::Check(order(d, recs) == "Brick1 Concrete2 Gypsum1 Insulation1 Insulation2 Plaster2 ", "components: option off sorts across walls by name");
  d.keepTogetherBy = "Element";
  d8test::Check(order(d, recs) == "Brick1 Insulation1 Gypsum1 Concrete2 Insulation2 Plaster2 ", "components: option on keeps each wall's skins in composite order");
  recs[3] = comp("W2", "Aluminium");
  d8test::Check(order(d, recs) == "Aluminium2 Insulation2 Plaster2 Brick1 Insulation1 Gypsum1 ", "components: walls are ordered by their first component");
}

// D8-ARCHICAD-SCHED-0034: list only the floor-plan selection that also satisfies the criteria. 40 walls w1..w40, Height 3 for odd numbers
// and 2 for even; selection w3 w7 w8 w20 w33. Selection only: exactly those five rows. With Height > 2 as well: w3 w7 w33. A saved view
// does not store the selection: it lists all 40.
D8_EXTRA_TEST(Rw3Doc2ScheduleSelectionOnly) {
  std::vector<Record> recs;
  for (int i = 1; i <= 40; ++i) recs.push_back(Rec2({{"Id", S2(("w" + std::to_string(i)).c_str())}, {"Height", N2(i % 2 ? 3 : 2)}}));
  ScheduleDef d;
  d.fields = {P2("Id")};
  d.selectedOnly = true;
  d.selection = {"w3", "w7", "w8", "w20", "w33"};
  auto ids = [&](const ScheduleDef& def) { std::string s; for (auto& r : BuildSchedule(def, recs).rows) s += r.cells[0] + ","; return s; };
  d8test::Check(ids(d) == "w3,w7,w8,w20,w33,", "selection only: exactly the five selected walls");
  d.filters = {{"Height", Op::Gt, N2(2)}};
  d8test::Check(ids(d) == "w3,w7,w33,", "selection only: and the criteria");
  ScheduleDef saved = SaveAsView(d);
  saved.filters.clear();
  d8test::Check(!saved.selectedOnly && saved.selection.empty() && BuildSchedule(saved, recs).rows.size() == 40, "selection only: a saved view does not keep the selection");
}

// D8-ARCHICAD-TEAM-0038: snapshots on a schedule with keep-last-N. Hourly, keep 3: ticks at 0 (v0), 1800 (too early, none), 3600 (v1),
// 7200 (v2), 10800 (v3), 14400 (v4) leave the snapshots taken at 7200, 10800, 14400. Rolling back to 10800 gives the saved v3; 0 is gone.
// Daily, keep 2: ticks at 0, 3600 (no), 86400, 172800 leave 86400 and 172800. The scheduling service itself is not tested.
D8_EXTRA_TEST(Rw3Doc2SnapshotRetention) {
  SnapshotStore h(SnapshotPolicy{3600, 3});
  d8test::Check(h.Tick(0, "v0") && !h.Tick(1800, "x") && h.Tick(3600, "v1") && h.Tick(7200, "v2") && h.Tick(10800, "v3") && h.Tick(14400, "v4"), "snapshots: hourly ticks");
  d8test::Check(h.All().size() == 3 && h.All()[0].time == 7200 && h.All()[1].time == 10800 && h.All()[2].time == 14400, "snapshots: the last three are kept");
  std::string st;
  d8test::Check(h.Rollback(10800, st) && st == "v3" && !h.Rollback(0, st) && h.Rollback(7200, st) && st == "v2", "snapshots: rollback returns the saved state, pruned ones are gone");
  SnapshotStore dly(SnapshotPolicy{86400, 2});
  d8test::Check(dly.Tick(0, "a") && !dly.Tick(3600, "b") && dly.Tick(86400, "c") && dly.Tick(172800, "d") && dly.All().size() == 2 && dly.All()[0].time == 86400 && dly.All()[1].time == 172800, "snapshots: daily keep 2");
}

// D8-ARCHICAD-TEAM-0046: force a user out. U reserves e1 and e2 and edits e2 (unsent). A user without permission cannot force the leave
// (ownership stays). The administrator forces U: both reservations are free, the unsent edit is gone, U's send is refused (-1) and the
// server revision of e2 stays 1; V can then reserve both. U can join again unless removed from the user list (Join then returns false).
D8_EXTRA_TEST(Rw3Doc2ForceUserToLeave) {
  TeamProject p(false);
  p.AddElement("e1"); p.AddElement("e2");
  p.Join("U"); p.Join("V");
  p.Reserve("U", "e1"); p.Reserve("U", "e2"); p.Edit("U", "e2");
  TeamResult r = p.ForceLeave(false, "U");
  d8test::Check(!r.ok && r.code == "not-allowed" && p.OwnedBy("U").size() == 2, "force leave: refused without permission");
  d8test::Check(p.ForceLeave(true, "U").ok && p.OwnedBy("U").empty() && !p.HasUnsent("U"), "force leave: reservations freed, unsent edits discarded");
  d8test::Check(p.Send("U") == -1 && p.ServerRev("e2") == 1, "force leave: the later send is refused");
  d8test::Check(p.Reserve("V", "e1").ok && p.Reserve("V", "e2").ok, "force leave: another user can reserve both");
  d8test::Check(p.Join("U") && p.Send("U") == 0, "force leave: the user can join again");
  p.ForceLeave(true, "U", true);
  d8test::Check(!p.Join("U") && p.Send("U") == -1, "force leave: a user removed from the list cannot rejoin");
}

// D8-ARCHICAD-TEAM-0049: compare after a receive, 3D elements only. Before: wall1 3D, slab1 3D (Z 3), col1 3D, line1 2D (L 1).
// After: wall1, slab1 moved (Z 3.5), col1 gone, line1 changed (L 2), poly1 2D added, wall2 3D added. Result: new wall2; modified slab1
// (Z); deleted col1; the 2D changes are not listed and unchanged wall1 is not listed.
D8_EXTRA_TEST(Rw3Doc2CompareAfterReceiveListsOnly3D) {
  auto el = [](const char* id, const char* dim, const char* k, const char* v) { ModelElement e; e.id = id; e.props["Dim"] = dim; e.props[k] = v; return e; };
  std::vector<ModelElement> before = {el("wall1", "3D", "X", "0"), el("slab1", "3D", "Z", "3"), el("col1", "3D", "X", "5"), el("line1", "2D", "L", "1")};
  std::vector<ModelElement> after = {el("wall1", "3D", "X", "0"), el("slab1", "3D", "Z", "3.5"), el("line1", "2D", "L", "2"), el("poly1", "2D", "L", "9"), el("wall2", "3D", "X", "7")};
  ModelDiff d = DiffModels3D(before, after);
  d8test::Check(d.added == std::vector<std::string>({"wall2"}) && d.modified == std::vector<std::string>({"slab1"}) && d.removed == std::vector<std::string>({"col1"}) && d.changedProps["slab1"] == std::vector<std::string>({"Z"}),
                "compare 3D: new wall2, modified slab1 (Z), deleted col1");
  ModelDiff all = DiffModels(before, after);
  d8test::Check(all.added.size() == 2 && all.modified.size() == 2, "compare 3D: without the 3D filter the 2D changes would be listed too");
}

// D8-ARCHICAD-TEAM-0042: automatic reservation. With auto-reserve on, editing a free element reserves it for the actor. With it off the
// same edit is refused (not-reserved) until Reserve is used. An element reserved by another user is refused with a message naming the
// owner. An element changed on the server since the user's copy is refused (outdated) until received.
D8_EXTRA_TEST(Rw3Doc2AutoReserveOnEdit) {
  TeamProject on(true), off(false);
  for (TeamProject* p : {&on, &off}) { p->AddElement("w"); p->AddElement("s"); p->Join("a"); p->Join("b"); }
  d8test::Check(on.Edit("a", "w").ok && on.OwnerOf("w") == "a", "auto reserve on: the edit reserves the element");
  TeamResult r = off.Edit("a", "w");
  d8test::Check(!r.ok && r.code == "not-reserved" && off.OwnerOf("w").empty(), "auto reserve off: the edit is refused");
  d8test::Check(off.Reserve("a", "w").ok && off.Edit("a", "w").ok, "auto reserve off: reserve then edit");
  r = on.Edit("b", "w");
  d8test::Check(!r.ok && r.owner == "a" && r.message.find("owned by a") != std::string::npos, "auto reserve: another user's element is refused naming the owner");
  on.Send("a");
  r = on.Edit("b", "w");
  d8test::Check(!r.ok && r.code == "reserved", "auto reserve: still reserved after the send (owner keeps it until release)");
  on.Release("a", "w");
  r = on.Edit("b", "w");
  d8test::Check(!r.ok && r.code == "outdated", "auto reserve: a stale copy needs a receive first");
  on.Receive("b");
  d8test::Check(on.Edit("b", "w").ok && on.OwnerOf("w") == "b", "auto reserve: after the receive the edit reserves");
}

// D8-ARCHICAD-TEAM-0009: attribute types are reserved as a whole. A reserves Surface; B cannot modify a server surface (refused, A named)
// but can create a new one, which stays local until sent: A does not see it until B sends and A receives, then under the same name.
// ReserveAll by B while A holds Surface returns Surface as blocked and gives B the other types. Release lets B modify.
D8_EXTRA_TEST(Rw3Doc2AttributeReservationPerType) {
  AttributeProject p({"Surface", "Pen", "Layer"});
  p.Seed("Surface", "Brick", "red");
  p.Receive("A"); p.Receive("B");
  d8test::Check(p.Reserve("A", "Surface").ok, "attributes: A reserves Surface");
  TeamResult r = p.Modify("B", "Surface", "Brick", "blue");
  d8test::Check(!r.ok && r.code == "reserved" && r.owner == "A", "attributes: B is refused, A named");
  d8test::Check(p.Modify("B", "Pen", "P1", "x").code == "not-reserved", "attributes: a type nobody reserved cannot be modified either");
  d8test::Check(p.Modify("A", "Surface", "Brick", "orange").ok, "attributes: A modifies Brick");
  p.Create("B", "Surface", "Glass", "clear");
  std::string v;
  d8test::Check(p.Has("B", "Surface", "Glass", &v) && v == "clear" && !p.Has("A", "Surface", "Glass"), "attributes: a new surface stays local until sent");
  d8test::Check(p.Modify("B", "Surface", "Glass", "tinted").ok, "attributes: the creator can still change its unsent attribute");
  p.Send("B");
  d8test::Check(!p.Has("A", "Surface", "Glass"), "attributes: not visible to A before A receives");
  p.Receive("A");
  d8test::Check(p.Has("A", "Surface", "Glass", &v) && v == "tinted", "attributes: after the receive A sees the surface under the same name");
  p.Send("A");
  p.Receive("B");
  d8test::Check(p.Has("B", "Surface", "Brick", &v) && v == "orange", "attributes: A's change reaches B");
  std::vector<std::string> blocked = p.ReserveAll("B");
  d8test::Check(blocked == std::vector<std::string>({"Surface"}) && p.Reserve("B", "Pen").ok, "attributes: reserve all returns the blocked type");
  p.Release("A", "Surface");
  d8test::Check(p.Modify("B", "Surface", "Brick", "grey").code == "not-reserved" && p.Reserve("B", "Surface").ok && p.Modify("B", "Surface", "Brick", "grey").ok, "attributes: after the release B reserves Surface and can modify");
}

// D8-ARCHICAD-TEAM-0021: issue data model. Members anna, ben, cara. anna creates "Clash" (Open, High, type Clash, due 2026-12-01,
// elements w1 w2, view 3D-1): id 1, visible to ben and cara, not to an outsider. Invalid: empty title, unknown status, unknown priority,
// due 2026-02-29 (2026 is not a leap year), non-member creator; 2028-02-29 is valid. Assign to ben: notice to ben, responsible ben;
// an outsider cannot be assigned. On 2026-12-02 the issue is overdue; after status Closed it is not; status "Done" is refused.
D8_EXTRA_TEST(Rw3Doc2IssueDataModel) {
  IssueTracker t({"anna", "ben", "cara"});
  Issue i;
  i.title = "Clash"; i.status = "Open"; i.priority = "High"; i.type = "Clash"; i.due = "2026-12-01"; i.elements = {"w1", "w2"}; i.view = "3D-1";
  std::string err;
  int id = t.Create("anna", i, &err);
  d8test::Check(id == 1, "issue: created with id 1");
  auto seen = t.VisibleTo("ben");
  d8test::Check(seen.size() == 1 && seen[0].title == "Clash" && seen[0].creator == "anna" && seen[0].elements.size() == 2 && seen[0].view == "3D-1" && seen[0].due == "2026-12-01" && t.VisibleTo("cara").size() == 1 && t.VisibleTo("eve").empty(),
                "issue: visible to every member with all its data, to nobody else");
  Issue bad = i; bad.title = "";
  d8test::Check(t.Create("anna", bad, &err) == 0 && err == "title required", "issue: empty title refused");
  bad = i; bad.status = "Done";
  d8test::Check(t.Create("anna", bad) == 0, "issue: unknown status refused");
  bad = i; bad.priority = "Urgent";
  d8test::Check(t.Create("anna", bad) == 0, "issue: unknown priority refused");
  bad = i; bad.due = "2026-02-29";
  d8test::Check(t.Create("anna", bad) == 0 && !ValidDate("2026-02-29") && ValidDate("2028-02-29") && !ValidDate("2026-13-01") && !ValidDate("26-1-1"), "issue: due dates are validated (leap years)");
  d8test::Check(t.Create("eve", i) == 0, "issue: a non-member cannot create");
  d8test::Check(t.Assign(1, "ben") && t.VisibleTo("anna")[0].responsible == "ben" && t.Notices().size() == 1 && t.Notices()[0].to == "ben" && !t.Assign(1, "eve"), "issue: assignment notifies the new responsible member");
  d8test::Check(t.Overdue("2026-12-01").empty() && t.Overdue("2026-12-02") == std::vector<int>({1}), "issue: overdue strictly after the due date");
  d8test::Check(t.SetStatus(1, "Closed") && t.Overdue("2026-12-02").empty() && !t.SetStatus(1, "Done"), "issue: closed issues are not overdue, unknown status refused");
}

// D8-REVIT-WSHARE-0020: check out several worksets at once. Worksets a b c d. alice makes a b c editable in one action: all owned. bob
// tries a b c d: a, b, c refused naming alice, d granted. In workset a, bob's edit is refused naming alice; alice's edit succeeds and
// she is its editor without borrowing.
D8_EXTRA_TEST(Rw3Doc2WorksetCheckOutSeveral) {
  WorksetModel m;
  for (const char* n : {"a", "b", "c", "d"}) m.Create(n);
  m.AddElement("ea", "a");
  auto r = m.MakeEditable("alice", {"a", "b", "c"});
  d8test::Check(r["a"].ok && r["b"].ok && r["c"].ok && m.Get("a")->owner == "alice" && m.Get("c")->owner == "alice" && m.Get("d")->owner.empty(), "worksets: three owned in one action");
  r = m.MakeEditable("bob", {"a", "b", "c", "d"});
  d8test::Check(!r["a"].ok && r["a"].owner == "alice" && !r["b"].ok && !r["c"].ok && r["d"].ok && m.Get("d")->owner == "bob" && m.Get("a")->owner == "alice", "worksets: the second user cannot take them, gets d");
  TeamResult e = m.Edit("bob", "ea");
  d8test::Check(!e.ok && e.code == "owned" && e.owner == "alice" && e.message.find("alice") != std::string::npos, "worksets: bob's edit in a is refused naming alice");
  d8test::Check(m.Edit("alice", "ea").ok && m.EditorOf("ea") == "alice", "worksets: the owner edits and is the editor");
  d8test::Check(m.MakeEditable("bob", {"nope"})["nope"].code == "unknown", "worksets: an unknown workset is reported");
}

// D8-REVIT-WSHARE-0008 and D8-REVIT-WSHARE-0019: reassign elements to another workset; report who holds what. Elements e1 e2 e3 in Shared.
// bob edits e1 (borrows it): editor of e1 is bob; the report for Shared lists owner "" and borrowers {bob}. bob moves e1 and e2 to Mech:
// both report Mech. alice cannot move e1 (borrowed by bob). Moving into a workset owned by another user, or a closed one, moves nothing.
// After alice owns Mech, the report shows owner alice for Mech and borrower bob for e1's workset.
D8_EXTRA_TEST(Rw3Doc2WorksetReassignAndReport) {
  WorksetModel m;
  m.Create("Shared", WorksetKind::Standard); m.Create("Mech"); m.Create("Closed");
  m.AddElement("e1", "Shared"); m.AddElement("e2", "Shared"); m.AddElement("e3", "Shared");
  m.Edit("bob", "e1");
  auto rep = m.Report();
  d8test::Check(m.EditorOf("e1") == "bob" && rep["Shared"].owner.empty() && rep["Shared"].borrowers == std::set<std::string>({"bob"}) && m.EditorOf("e3").empty(), "workset report: owner and borrowers of Shared, editor of e1");
  d8test::Check(m.Reassign("alice", {"e1"}, "Mech").empty() && m.WorksetOf("e1") == "Shared", "reassign: an element borrowed by bob cannot be moved by alice");
  auto moved = m.Reassign("bob", {"e1", "e2"}, "Mech");
  d8test::Check(moved == std::vector<std::string>({"e1", "e2"}) && m.WorksetOf("e1") == "Mech" && m.WorksetOf("e2") == "Mech" && m.WorksetOf("e3") == "Shared", "reassign: moved elements report the new workset");
  m.SetOpen("Closed", false);
  d8test::Check(m.Reassign("bob", {"e3"}, "Closed").empty() && m.Reassign("bob", {"e3"}, "Missing").empty(), "reassign: a closed or unknown target moves nothing");
  m.MakeEditable("alice", {"Mech"});
  d8test::Check(m.Reassign("bob", {"e3"}, "Mech").empty(), "reassign: a target owned by another user moves nothing");
  rep = m.Report();
  d8test::Check(rep["Mech"].owner == "alice" && rep["Mech"].borrowers == std::set<std::string>({"bob"}) && rep["Shared"].borrowers.empty(), "workset report: after the moves Mech is owned by alice with borrower bob");
}

// D8-REVIT-COLLAB-0002: create, rename, open, close worksets, see owner and visibility. Create Walls and Doors; a duplicate name is refused.
// Elements w1 w2 in Walls, d1 in Doors. Renaming Walls to Shell moves the elements' workset; renaming to an existing name or renaming a
// family workset is refused. Closing Shell hides w1 w2 (visible: d1), editing them is refused as closed; opening shows all again. The
// owner of a workset is visible through Get after MakeEditable.
D8_EXTRA_TEST(Rw3Doc2WorksetCreateRenameOpenClose) {
  WorksetModel m;
  d8test::Check(m.Create("Walls") && m.Create("Doors") && !m.Create("Walls") && !m.Create(""), "worksets: create, duplicate and empty names refused");
  m.Create("Chair", WorksetKind::Family);
  m.AddElement("w1", "Walls"); m.AddElement("w2", "Walls"); m.AddElement("d1", "Doors");
  d8test::Check(m.Rename("Walls", "Shell") && m.WorksetOf("w1") == "Shell" && !m.Get("Walls") && m.Get("Shell"), "worksets: rename carries the elements");
  d8test::Check(!m.Rename("Shell", "Doors") && !m.Rename("Chair", "Seat") && !m.Rename("Nope", "X"), "worksets: rename to an existing name, of a family workset or of an unknown one is refused");
  d8test::Check(m.VisibleElements() == std::vector<std::string>({"w1", "w2", "d1"}), "worksets: all open, all elements visible");
  m.SetOpen("Shell", false);
  d8test::Check(m.VisibleElements() == std::vector<std::string>({"d1"}) && !m.Get("Shell")->open, "worksets: closing hides its elements");
  TeamResult e = m.Edit("u", "w1");
  d8test::Check(!e.ok && e.code == "closed", "worksets: a closed workset's element cannot be edited");
  m.SetOpen("Shell", true);
  d8test::Check(m.VisibleElements().size() == 3 && m.Edit("u", "w1").ok, "worksets: opening shows them again");
  m.MakeEditable("u", {"Doors"});
  d8test::Check(m.Get("Doors")->owner == "u" && m.Get("Shell")->owner.empty(), "worksets: owners are visible");
}

// D8-REVIT-WSHARE-0013: model updates by other users. alice edits W1 and deletes W2 and sends; W3 untouched. Before bob receives, his
// model updates list W1 as updated and W2 as deleted (W3 not listed, alice's own list is empty); bob's edit of the deleted W2 is refused
// as deleted, of the outdated W1 as outdated. After bob receives the lists are empty and W1 is editable.
D8_EXTRA_TEST(Rw3Doc2WorksharingModelUpdates) {
  TeamProject p;
  p.AddElement("W1"); p.AddElement("W2"); p.AddElement("W3");
  p.Join("alice"); p.Join("bob");
  p.Edit("alice", "W1");
  p.Delete("alice", "W2");
  d8test::Check(p.Send("alice") == 2 && p.IsDeleted("W2") && !p.IsDeleted("W1"), "model updates: alice sends an edit and a deletion");
  auto u = p.ModelUpdates("bob");
  d8test::Check(u.updated == std::vector<std::string>({"W1"}) && u.deleted == std::vector<std::string>({"W2"}), "model updates: bob sees W1 updated and W2 deleted, not W3");
  auto own = p.ModelUpdates("alice");
  d8test::Check(own.updated.empty() && own.deleted.empty(), "model updates: the author has none of his own");
  d8test::Check(p.Edit("bob", "W2").code == "deleted" && p.Edit("bob", "W1").code == "reserved", "model updates: a deleted element and a still-borrowed one are refused");
  p.Release("alice", "W1");
  d8test::Check(p.Edit("bob", "W1").code == "outdated", "model updates: an outdated element is refused until received");
  p.Receive("bob");
  u = p.ModelUpdates("bob");
  d8test::Check(u.updated.empty() && u.deleted.empty() && p.Edit("bob", "W1").ok, "model updates: after the receive nothing is flagged");
}

}  // namespace
