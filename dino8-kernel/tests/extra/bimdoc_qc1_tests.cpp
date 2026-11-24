// Review-fix tests for bimdoc_calc.h and bimdoc3_calc.h. Three defects were found by an independent review:
//   (1) Prepare() applied every formula field fields+1 times, so a formula reading a value already stored under its own name was applied
//       repeatedly, a cycle over records that hold both keys grew without bound, and formulas were evaluated before the Combined fields;
//   (2) the schedule scheme XML dropped a number of Field and ScheduleDef settings;
//   (3) PreviewImport read the id cell of an empty row.
// Each test below fails on the old code (decoy values that the old code changes, member-by-member comparison with the original structure,
// ragged and empty rows) and every expected value is worked out by hand in its comment.
#include <cmath>
#include <string>
#include <type_traits>
#include <vector>

#include "dino8/kernel/bimdoc3_calc.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::bimdoc;

Value Sq(const char* s) { return Value::Of(s); }
Value Nq(double d) { return Value::Of(d); }
Record Rq(std::initializer_list<std::pair<const char*, Value>> kv) { Record r; for (auto& p : kv) r[p.first] = p.second; return r; }
Field Pq(const std::string& n, int decimals = -1) { Field f; f.name = n; f.decimals = decimals; return f; }
Field Eq(const char* n, const char* expr, int dec = -1) { Field f; f.name = n; f.kind = FieldKind::Formula; f.formula = expr; f.decimals = dec; return f; }
bool Nearq(double a, double b) { return std::fabs(a - b) <= 1e-9; }

// Counts formula evaluations when Prepare() offers the counter; on code without it the count stays -1 so the test fails instead of failing to compile.
template <class D, class R>
auto CountedPrepare(const D& d, const R& r, int* n, int) -> decltype(Prepare(d, r, n)) { *n = 0; return Prepare(d, r, n); }
template <class D, class R>
std::vector<Record> CountedPrepare(const D& d, const R& r, int* n, long) { *n = -1; return Prepare(d, r); }

// BIM-1 (a): a formula whose own name already exists in the record is evaluated once and replaces that value once.
// Fields Id, Area = Area*1000; records Area = 2 and Area = 0.5 give 2000 and 500 (not 2e12 and 5e8 from four passes). A record without an
// Area key has nothing to multiply and shows not evaluated. D8-REVIT-SCHED-0002 / D8-ARCHICAD-SCHED-0025.
D8_EXTRA_TEST(Rw3QcPrepareSingleEvaluationOfSameNamedKey) {
  ScheduleDef d;
  d.fields = {Pq("Id"), Eq("Area", "Area*1000", 0), Pq("Pad"), Pq("Pad2")};
  std::vector<Record> recs = {Rq({{"Id", Sq("a")}, {"Area", Nq(2)}}), Rq({{"Id", Sq("b")}, {"Area", Nq(0.5)}}), Rq({{"Id", Sq("c")}})};
  std::vector<Record> p = Prepare(d, recs);
  d8test::Check(Nearq(p[0]["Area"].n, 2000) && Nearq(p[1]["Area"].n, 500), "single evaluation: the stored value is used once (2 -> 2000, 0.5 -> 500)");
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(r.rows[0].cells[1] == "2000" && r.rows[1].cells[1] == "500" && r.rows[2].cells[1] == "-", "single evaluation: the table shows 2000, 500 and not evaluated");
  d.fields[1].formula = "Area+1";
  p = Prepare(d, recs);
  d8test::Check(Nearq(p[0]["Area"].n, 3) && Nearq(p[1]["Area"].n, 1.5), "single evaluation: Area+1 adds one once (3, 1.5), however many fields the schedule has");
}

// BIM-1 (b): a cycle over records that already hold both keys. Loop1 = Loop2+1, Loop2 = Loop1+1. Record 1 holds Loop1 = 5, Loop2 = 7: in field
// order Loop1 = 7+1 = 8 and then Loop2 = 8+1 = 9, and nothing grows afterwards. Record 2 holds only Loop1 = 5: Loop1 reads an undefined Loop2 and
// both end up not evaluated. Record 3 holds neither: both not evaluated.
D8_EXTRA_TEST(Rw3QcPrepareCycleOverHeldKeysDoesNotGrow) {
  ScheduleDef d;
  d.fields = {Eq("Loop1", "Loop2+1", 0), Eq("Loop2", "Loop1+1", 0)};
  std::vector<Record> recs = {Rq({{"Loop1", Nq(5)}, {"Loop2", Nq(7)}}), Rq({{"Loop1", Nq(5)}}), Rq({})};
  std::vector<Record> p = Prepare(d, recs);
  d8test::Check(Nearq(p[0]["Loop1"].n, 8) && Nearq(p[0]["Loop2"].n, 9), "cycle: both keys held -> 8 and 9, no growth");
  d8test::Check(!p[1]["Loop1"].Defined() && !p[1]["Loop2"].Defined() && !p[2]["Loop1"].Defined() && !p[2]["Loop2"].Defined(), "cycle: one key or none held -> not evaluated");
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(r.rows[0].cells == std::vector<std::string>({"8", "9"}) && r.rows[1].cells == std::vector<std::string>({"-", "-"}) && r.rows[2].cells == std::vector<std::string>({"-", "-"}), "cycle: the table rows");
}

// BIM-1 (c): field order against Combined fields. Fields Mark, Width, Label (Combined Mark and Width, separator "-"), EchoAfter = if(1,Label,0)
// declared after Label, EchoBefore = if(1,Label2,0) declared before the Combined field Label2. A formula sees the Combined fields declared earlier
// ("D1-0.9") and not those declared later (not evaluated), also after the retry sweep for chains. A chain Cost = Area*Rate declared before
// Area = Width*2 still resolves: Area 1.8, Cost 180.
D8_EXTRA_TEST(Rw3QcPrepareCombinedFieldsKeepDeclarationOrder) {
  ScheduleDef d;
  Field label; label.name = "Label"; label.kind = FieldKind::Combined; label.separator = "-"; label.parts = {{"Mark", "", "", -1}, {"Width", "", "", -1}};
  Field label2 = label; label2.name = "Label2";
  d.fields = {Pq("Mark"), Pq("Width", 1), label, Eq("EchoAfter", "if(1,Label,0)"), Eq("EchoBefore", "if(1,Label2,0)"), label2, Eq("Cost", "Area*Rate", 0), Eq("Area", "Width*2", 1)};
  std::vector<Record> recs = {Rq({{"Mark", Sq("D1")}, {"Width", Nq(0.9)}, {"Rate", Nq(100)}})};
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(r.rows[0].cells == std::vector<std::string>({"D1", "0.9", "D1-0.9", "D1-0.9", "-", "D1-0.9", "180", "1.8"}), "declaration order: Echo after sees Label, Echo before does not, the Cost chain resolves");
}

// BIM-1 (d): evaluation stops as soon as nothing new is defined. The counter is the number of formula evaluations.
//  - One formula among 30 plain fields, 3 records: 3 evaluations (not 31 per record).
//  - Chain C = B+1, B = A+1, A = W declared in that order, 2 records: pass 0 evaluates 3 fields; sweep 1 re-evaluates C and B (B becomes defined);
//    sweep 2 re-evaluates C (defined); the next sweep finds nothing: 3 + 2 + 1 = 6 per record, 12 in all; results C = W+2.
//  - A 2-cycle among 30 plain fields, 2 records: pass 0 (2) + one sweep without progress (2) = 4 per record, 8 in all.
//  - A formula with a missing input (Cost = Area*Rate, no Rate), 2 records: pass 0 (2) + one sweep (1) = 3 per record, 6.
D8_EXTRA_TEST(Rw3QcPrepareSettlesEarly) {
  ScheduleDef one;
  for (int i = 0; i < 30; ++i) one.fields.push_back(Pq("P" + std::to_string(i)));
  one.fields.push_back(Eq("F", "P0*2"));
  std::vector<Record> three = {Rq({{"P0", Nq(1)}}), Rq({{"P0", Nq(2)}}), Rq({{"P0", Nq(3)}})};
  int n = 0;
  CountedPrepare(one, three, &n, 0);
  d8test::Check(n == 3, "settling: one formula among 30 fields costs one evaluation per record");
  ScheduleDef chain;
  chain.fields = {Eq("C", "B+1", 0), Eq("B", "A+1", 0), Eq("A", "W", 0)};
  std::vector<Record> two = {Rq({{"W", Nq(10)}}), Rq({{"W", Nq(20)}})};
  std::vector<Record> p = CountedPrepare(chain, two, &n, 0);
  d8test::Check(n == 12 && Nearq(p[0]["C"].n, 12) && Nearq(p[1]["C"].n, 22) && Nearq(p[0]["B"].n, 11), "settling: reversed chain of three: 6 evaluations per record, values W+2 and W+1");
  ScheduleDef cyc;
  for (int i = 0; i < 30; ++i) cyc.fields.push_back(Pq("P" + std::to_string(i)));
  cyc.fields.push_back(Eq("L1", "L2+1")); cyc.fields.push_back(Eq("L2", "L1+1"));
  p = CountedPrepare(cyc, two, &n, 0);
  d8test::Check(n == 8 && !p[0]["L1"].Defined() && !p[1]["L2"].Defined(), "settling: a cycle among 30 fields costs 4 evaluations per record");
  ScheduleDef miss;
  miss.fields = {Eq("Cost", "Area*Rate"), Eq("Area", "W*2")};
  p = CountedPrepare(miss, two, &n, 0);
  d8test::Check(n == 6 && !p[0]["Cost"].Defined() && Nearq(p[0]["Area"].n, 20), "settling: a formula with a missing input is tried once more, then left");
}

// ------------------------------------------------------------------------------------------------------------------------------
// Scheme XML: member-by-member comparison against the original structures
bool SameValue(const Value& a, const Value& b) { return a.kind == b.kind && (a.kind != Value::Num || a.n == b.n) && (a.kind != Value::Str || a.s == b.s); }
bool SamePart(const CombinedPart& a, const CombinedPart& b) { return a.field == b.field && a.prefix == b.prefix && a.suffix == b.suffix && a.decimals == b.decimals; }
bool SameField(const Field& a, const Field& b) {
  if (a.parts.size() != b.parts.size()) return false;
  for (size_t i = 0; i < a.parts.size(); ++i) if (!SamePart(a.parts[i], b.parts[i])) return false;
  return a.name == b.name && a.kind == b.kind && a.formula == b.formula && a.percentOf == b.percentOf && a.percentBy == b.percentBy && a.separator == b.separator &&
         a.aggregate == b.aggregate && a.decimals == b.decimals && a.scale == b.scale && a.unit == b.unit && a.showUnit == b.showUnit && a.mergeAny == b.mergeAny &&
         a.mergeShow == b.mergeShow && a.mergeAppendQuantity == b.mergeAppendQuantity && a.listSeparator == b.listSeparator && a.zeroValueText == b.zeroValueText &&
         a.zeroValue == b.zeroValue && a.zeroTotalText == b.zeroTotalText && a.zeroTotal == b.zeroTotal && a.zeroGrandText == b.zeroGrandText && a.zeroGrand == b.zeroGrand &&
         a.notEvaluated == b.notEvaluated && a.minMaxSeparator == b.minMaxSeparator && a.totalPrefix == b.totalPrefix && a.totalSuffix == b.totalSuffix && a.hidden == b.hidden;
}
bool SameDef(const ScheduleDef& a, const ScheduleDef& b) {
  if (a.fields.size() != b.fields.size() || a.filters.size() != b.filters.size() || a.sort.size() != b.sort.size()) return false;
  for (size_t i = 0; i < a.fields.size(); ++i) if (!SameField(a.fields[i], b.fields[i])) return false;
  for (size_t i = 0; i < a.filters.size(); ++i)
    if (a.filters[i].field != b.filters[i].field || a.filters[i].op != b.filters[i].op || !SameValue(a.filters[i].value, b.filters[i].value)) return false;
  for (size_t i = 0; i < a.sort.size(); ++i)
    if (a.sort[i].field != b.sort[i].field || a.sort[i].descending != b.sort[i].descending || a.sort[i].header != b.sort[i].header || a.sort[i].footer != b.sort[i].footer || a.sort[i].blankAfter != b.sort[i].blankAfter) return false;
  return a.itemize == b.itemize && a.grandTotal == b.grandTotal && a.roundedGrandTotals == b.roundedGrandTotals && a.filterBySheet == b.filterBySheet && a.sheet == b.sheet && a.keepTogetherBy == b.keepTogetherBy && a.idField == b.idField;
}

// BIM-2: every setting survives. One scheme holds a field with EVERY member set to a value that differs from its default (and from the other
// fields), a combined field with two parts, three criteria (number, text, undefined value, with different operators), two sort keys and every schedule
// flag. After export and import, each Field member and each ScheduleDef member equals the original (member by member, not only text after text).
// The floor-plan selection is not part of a scheme (as for saved views): a scheme exported with a selection comes back without it.
D8_EXTRA_TEST(Rw3QcSchemeXmlKeepsEverySetting) {
  Field all;
  all.name = "Qty \"x\" & <y>"; all.kind = FieldKind::Percent; all.formula = "A*B"; all.percentOf = "Of&1"; all.percentBy = "By<2>"; all.separator = " ; ";
  all.aggregate = Aggregate::MinMax; all.decimals = 3; all.scale = 0.001953125; all.unit = " m\"2"; all.showUnit = true; all.mergeAny = true; all.mergeShow = MergeShow::List;
  all.mergeAppendQuantity = false; all.listSeparator = " | "; all.zeroValueText = true; all.zeroValue = "n/a"; all.zeroTotalText = true; all.zeroTotal = "none (tot)";
  all.zeroGrandText = true; all.zeroGrand = "none (grand)"; all.notEvaluated = "?"; all.minMaxSeparator = " .. "; all.totalPrefix = "<"; all.totalSuffix = ">"; all.hidden = true;
  Field comb; comb.name = "Size"; comb.kind = FieldKind::Combined; comb.separator = " x "; comb.parts = {{"W", "w=", "mm", 2}, {"H", "", "", -1}};
  ScheduleDef d;
  d.fields = {all, comb, Pq("Plain", 1)};
  d.filters = {{"W", Op::Le, Nq(1.5)}, {"Type", Op::NotContains, Sq("x\"&")}, {"Fire", Op::HasValue, Value()}};
  SortKey k1; k1.field = "Type"; k1.descending = true; k1.header = true; k1.footer = true; k1.blankAfter = true;
  SortKey k2; k2.field = "W";
  d.sort = {k1, k2};
  d.itemize = false; d.grandTotal = true; d.roundedGrandTotals = true; d.filterBySheet = true; d.sheet = "A-101 & <B>"; d.keepTogetherBy = "Element"; d.idField = "Guid";
  d.selectedOnly = true; d.selection = {"w1", "w2"};
  SchemeHeader h{"S9", "Everything", "Component"};
  std::string xml = ExportSchemeXml(d, h);
  ScheduleDef back; SchemeHeader hb;
  d8test::Check(ImportSchemeXml(xml, back, hb) && hb.id == "S9" && hb.name == "Everything" && hb.type == "Component", "scheme settings: import accepted, header kept");
  ScheduleDef expect = d; expect.selectedOnly = false; expect.selection.clear();
  d8test::Check(SameDef(expect, back), "scheme settings: every Field and ScheduleDef member equals the original");
  d8test::Check(!back.selectedOnly && back.selection.empty(), "scheme settings: the transient selection is not stored");
  // decoys: flip every boolean and the strings to be sure the comparison is not vacuous
  ScheduleDef decoy = back;
  decoy.fields[0].mergeAppendQuantity = true;
  d8test::Check(!SameDef(expect, decoy), "scheme settings: the comparison notices a changed member");
  decoy = back; decoy.fields[0].zeroGrand = "x";
  d8test::Check(!SameDef(expect, decoy), "scheme settings: the comparison notices a changed text");
  decoy = back; decoy.idField = "Id";
  d8test::Check(!SameDef(expect, decoy), "scheme settings: the comparison notices a changed schedule setting");

  // the same settings drive the table: records with a sheet list, zero values, merged widths and min / max
  ScheduleDef t;
  Field w = Pq("Width", 1); w.mergeAny = true; w.mergeShow = MergeShow::List; w.mergeAppendQuantity = false; w.listSeparator = "; ";
  Field q = Pq("Qty", 0); q.aggregate = Aggregate::MinMax; q.minMaxSeparator = " .. "; q.zeroValueText = true; q.zeroValue = "n/a"; q.zeroTotalText = true; q.zeroTotal = "T0"; q.zeroGrandText = true; q.zeroGrand = "G0";
  t.fields = {Pq("Type"), w, q};
  SortKey ts; ts.field = "Type"; ts.footer = true;
  t.sort = {ts}; t.itemize = false; t.grandTotal = true; t.filterBySheet = true; t.sheet = "A101";
  std::vector<Record> recs = {Rq({{"Type", Sq("D")}, {"Width", Nq(0.9)}, {"Qty", Nq(0)}, {"Sheets", Sq("A101")}}), Rq({{"Type", Sq("D")}, {"Width", Nq(1.0)}, {"Qty", Nq(0)}, {"Sheets", Sq("A101;A102")}}),
                              Rq({{"Type", Sq("E")}, {"Width", Nq(1.2)}, {"Qty", Nq(5)}, {"Sheets", Sq("A102")}})};
  ScheduleDef tb; SchemeHeader th;
  ImportSchemeXml(ExportSchemeXml(t, SchemeHeader{"T", "T", "Element"}), tb, th);
  ScheduleResult a = BuildSchedule(t, recs), b = BuildSchedule(tb, recs);
  bool same = a.rows.size() == b.rows.size();
  for (size_t i = 0; same && i < a.rows.size(); ++i) same = a.rows[i].kind == b.rows[i].kind && a.rows[i].cells == b.rows[i].cells && a.rows[i].count == b.rows[i].count && a.rows[i].label == b.rows[i].label;
  d8test::Check(same, "scheme settings: the imported scheme builds the same table");
  // by hand: only the two D records are on sheet A101; merged into one row: width list "0.9; 1.0" without quantities, quantity zero -> "n/a";
  // footer of D: min .. max of {0, 0} = "T0 .. T0"; grand: "G0 .. G0"
  d8test::Check(a.rows.size() == 3 && a.rows[0].cells == std::vector<std::string>({"D", "0.9; 1.0", "n/a"}) && a.rows[1].cells[2] == "T0 .. T0" && a.rows[2].cells[2] == "G0 .. G0", "scheme settings: the table shows the list separator, zero texts and the min .. max separator");
  // older files without the new attributes still load, with the defaults
  ScheduleDef old; SchemeHeader oh;
  d8test::Check(ImportSchemeXml("<Scheme id=\"O\" name=\"Old\" type=\"Element\" itemize=\"0\" grandTotal=\"1\" rounded=\"0\" keepTogether=\"\">\n<Field name=\"A\" kind=\"0\" decimals=\"2\" scale=\"1\"/>\n</Scheme>\n", old, oh) &&
                    old.fields.size() == 1 && old.fields[0].mergeAppendQuantity && old.fields[0].listSeparator == ", " && old.fields[0].minMaxSeparator == " - " && old.fields[0].notEvaluated == "-" && old.idField == "Id" && !old.itemize && old.grandTotal,
                "scheme settings: an older file loads with the defaults for the new settings");
  d8test::Check(!ImportSchemeXml("<Scheme id=\"O\"><Field name=\"A\" decimals=\"two\"/></Scheme>", old, oh), "scheme settings: a malformed number is refused");
}

// BIM-5: PreviewImport with ragged, blank and short rows. Model E1 (FireRating FD30, Cost 100), E2 (FD60, 200). The returned sheet has the header,
// E1 with a new rating, an EMPTY row, an all-blank row, a short row holding only "E2" (nothing to change), a short row "E2","FD30" (rating FD60 ->
// FD30), a row without an id but with cells (reported as unmatched "row 7": the seventh row of the sheet counting the header), a row longer than the
// header whose extra cells are ignored (E2 with its current values), and a trailing empty row. Changes: E1 FireRating FD30 -> FD60 and E2 FireRating FD60 -> FD30; unmatched
// "row 7"; nothing invalid. An empty sheet, an empty header and a header alone give no result. With an empty model every non-blank row is unmatched.
D8_EXTRA_TEST(Rw3QcPreviewImportToleratesRaggedAndBlankRows) {
  std::vector<Record> model = {Rq({{"Id", Sq("E1")}, {"FireRating", Sq("FD30")}, {"Cost", Nq(100)}}), Rq({{"Id", Sq("E2")}, {"FireRating", Sq("FD60")}, {"Cost", Nq(200)}})};
  Sheet sheet = {{"Id", "FireRating", "Cost"}, {"E1", "FD60", "100"}, {}, {"", "", ""}, {"E2"}, {"E2", "FD30"}, {"", "FD30", "1"}, {"E2", "FD60", "200", "extra", "more"}, {}};
  ImportPreview p = PreviewImport(model, "Id", sheet, {});
  d8test::Check(p.changes.size() == 2 && p.changes[0].id == "E1" && p.changes[0].prop == "FireRating" && p.changes[0].oldText == "FD30" && p.changes[0].newText == "FD60" &&
                    p.changes[1].id == "E2" && p.changes[1].prop == "FireRating" && p.changes[1].oldText == "FD60" && p.changes[1].newText == "FD30",
                "ragged import: the two real changes");
  d8test::Check(p.unmatched == std::vector<std::string>({"row 7"}) && p.invalid.empty(), "ragged import: the id-less row is unmatched, empty and short rows are not");
  d8test::Check(PreviewImport(model, "Id", {}, {}).changes.empty() && PreviewImport(model, "Id", Sheet{std::vector<std::string>{}}, {}).changes.empty() && PreviewImport(model, "Id", {{"Id", "Cost"}}, {}).changes.empty(), "ragged import: empty sheet, empty header, header only");
  ImportPreview none = PreviewImport({}, "Id", sheet, {});
  d8test::Check(none.changes.empty() && none.unmatched == std::vector<std::string>({"E1", "E2", "E2", "row 7", "E2"}), "ragged import: with no elements every non-blank row is unmatched, blank ones are skipped");
  Sheet tail = {{"Id", "Cost"}, {"E1", "300"}, {}};
  ImportPreview t = PreviewImport(model, "Id", tail, {});
  d8test::Check(t.changes.size() == 1 && t.changes[0].newText == "300", "ragged import: a trailing empty row after a good row");
  std::vector<Record> m2 = model;
  ApplyImport(m2, "Id", {{"E1", "Cost", "100", "abc"}, {"E2", "FireRating", "FD60", "FD90"}});
  d8test::Check(Nearq(m2[0]["Cost"].n, 100) && m2[1]["FireRating"].s == "FD90", "ragged import: applying a non-number to a number property leaves it, a text change applies");
}

}  // namespace
