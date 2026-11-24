// Tests of include/dino8/kernel/bimdoc3_calc.h (third documentation batch): renovation filters, schedule schemes as XML, field catalog,
// header rows, editable cells and overrides, unit formats, expression properties, property sheet import, headline rows, splitting,
// tag labels and conditional formatting. One test per Register row (or per pair of rows sharing a scenario). Every expected value is worked
// out by hand in the comment of the test. The vendor pages could not be fetched from this environment, so scenarios come from the rows'
// own statements and acceptance sketches; a behaviour the row does not state is called "assumption" and is listed in the row notes.
// Selection, undo, UI, drawing and file-format clauses are not tested.
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/bimdoc3_calc.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::bimdoc;

Field F3(const std::string& n, int decimals = -1) { Field f; f.name = n; f.decimals = decimals; return f; }
Value S3(const char* s) { return Value::Of(s); }
Value N3(double d) { return Value::Of(d); }
Record R3(std::initializer_list<std::pair<const char*, Value>> kv) { Record r; for (auto& p : kv) r[p.first] = p.second; return r; }
bool Near3(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }
std::string Cat(const std::vector<std::string>& v, const char* sep = ",") { std::string s; for (auto& x : v) s += x + sep; return s; }

RenoFilter MakeFilter(const char* name, RenoShow ex, RenoShow de, RenoShow nw, bool excludeAnnot = true) {
  RenoFilter f; f.name = name; f.show[0] = ex; f.show[1] = de; f.show[2] = nw; f.excludeAnnotations = excludeAnnot; return f;
}

// D8-ARCHICAD-RENO-0002 and D8-ARCHICAD-RENO-0006: named filters choose per status show, hide or override; the active filter decides the
// look in plan, section and 3D views. Elements w1 (Existing), w2 (Demolished), w3 (New) and a dimension dim1 (annotation, Demolished).
// Filter "Demolition": Existing show, Demolished override with rule "Dashed", New hide -> w1 normal, w2 override, w3 hidden; the annotation
// stays normal (annotations are excluded from the overrides). Filter "Plan": Demolished hide, New show -> w1 normal, w2 hidden, w3 normal.
// A filter that includes annotations and hides Demolished hides dim1. The same filter gives the same look in the plan, section and 3D view.
D8_EXTRA_TEST(Rw4DocRenovationFilterShowHideOverride) {
  RenovationModel m;
  m.defaultStatus = RenoStatus::Existing; m.Add("w1", "Wall");
  m.defaultStatus = RenoStatus::Demolished; m.Add("w2", "Wall"); m.Add("dim1", "Dimension", "", true);
  m.defaultStatus = RenoStatus::New; m.Add("w3", "Wall");
  RenoFilter demo = MakeFilter("Demolition", RenoShow::Show, RenoShow::Override, RenoShow::Hide); demo.rule[1] = "Dashed";
  d8test::Check(m.AddFilter(demo) && m.AddFilter(MakeFilter("Plan", RenoShow::Show, RenoShow::Hide, RenoShow::Show)) && m.AddFilter(MakeFilter("Strict", RenoShow::Show, RenoShow::Hide, RenoShow::Show, false)) &&
                    !m.AddFilter(MakeFilter("Plan", RenoShow::Show, RenoShow::Show, RenoShow::Show)),
                "renovation filters: three created, a duplicate name refused");
  m.SetViewFilter("plan", "Demolition"); m.SetViewFilter("section", "Demolition"); m.SetViewFilter("3d", "Demolition");
  for (const char* v : {"plan", "section", "3d"})
    d8test::Check(m.LookInView("w1", v) == RenoLook::Normal && m.LookInView("w2", v) == RenoLook::Override && m.LookInView("w3", v) == RenoLook::Hidden && m.LookInView("dim1", v) == RenoLook::Normal,
                  "renovation filters: Demolition gives normal / override / hidden, annotation untouched, in every view type");
  d8test::Check(Cat(m.VisibleIn("plan")) == "w1,w2,dim1,", "renovation filters: overridden elements still count as shown");
  m.SetViewFilter("plan", "Plan");
  d8test::Check(m.LookInView("w2", "plan") == RenoLook::Hidden && m.LookInView("w3", "plan") == RenoLook::Normal && Cat(m.VisibleIn("plan")) == "w1,dim1,w3,", "renovation filters: Plan hides the demolished wall");
  m.SetViewFilter("plan", "Strict");
  d8test::Check(m.LookInView("dim1", "plan") == RenoLook::Hidden, "renovation filters: a filter that includes annotations hides the demolished dimension");
  d8test::Check(m.Filter("Demolition")->rule[1] == "Dashed", "renovation filters: the override rule is kept");
}

// D8-ARCHICAD-RENO-0003: each view has its own active filter. Filters A (hides Demolished) and B (hides New). plan -> A, section -> B:
// w2 (Demolished) is hidden in plan and shown in section; w3 (New) the other way round. Switching plan to B leaves section on B and
// unchanged; an unknown filter is refused and changes nothing; a view without a filter shows everything normally.
D8_EXTRA_TEST(Rw4DocRenovationActiveFilterPerView) {
  RenovationModel m;
  m.defaultStatus = RenoStatus::Demolished; m.Add("w2", "Wall");
  m.defaultStatus = RenoStatus::New; m.Add("w3", "Wall");
  m.AddFilter(MakeFilter("A", RenoShow::Show, RenoShow::Hide, RenoShow::Show));
  m.AddFilter(MakeFilter("B", RenoShow::Show, RenoShow::Show, RenoShow::Hide));
  d8test::Check(m.SetViewFilter("plan", "A") && m.SetViewFilter("section", "B"), "per-view filter: set on two views");
  d8test::Check(m.LookInView("w2", "plan") == RenoLook::Hidden && m.LookInView("w3", "plan") == RenoLook::Normal && m.LookInView("w2", "section") == RenoLook::Normal && m.LookInView("w3", "section") == RenoLook::Hidden, "per-view filter: each view follows its own filter");
  m.SetViewFilter("plan", "B");
  d8test::Check(m.ViewFilter("plan") == "B" && m.ViewFilter("section") == "B" && Cat(m.VisibleIn("section")) == "w2,", "per-view filter: switching plan does not touch section");
  d8test::Check(!m.SetViewFilter("plan", "Nope") && m.ViewFilter("plan") == "B", "per-view filter: unknown filter refused");
  d8test::Check(m.ViewFilter("3d").empty() && Cat(m.VisibleIn("3d")) == "w2,w3,", "per-view filter: a view without a filter shows everything");
}

// D8-ARCHICAD-RENO-0001 and D8-ARCHICAD-RENO-0008: status decides how elements count in schedules; schedule views obey the view's filter, so
// two lists with different filters give demolished and new quantities. Walls (status, volume): e1 Existing 10, e2 Existing 4, d1 Demolished 6,
// d2 Demolished 3, n1 New 8, n2 New 5. View "demo list" filter shows only Demolished: rows d1 d2, total 9. View "new list" shows only New:
// n1 n2, total 13. View "existing list": e1 e2, total 14. A view without filter lists all six, total 36 = 14 + 9 + 13.
D8_EXTRA_TEST(Rw4DocRenovationScheduleObeysFilter) {
  RenovationModel m;
  struct E { const char* id; RenoStatus s; double q; };
  E es[] = {{"e1", RenoStatus::Existing, 10}, {"e2", RenoStatus::Existing, 4}, {"d1", RenoStatus::Demolished, 6}, {"d2", RenoStatus::Demolished, 3}, {"n1", RenoStatus::New, 8}, {"n2", RenoStatus::New, 5}};
  for (auto& e : es) { m.defaultStatus = e.s; m.Add(e.id, "Wall", "", false, e.q); }
  m.AddFilter(MakeFilter("OnlyDemo", RenoShow::Hide, RenoShow::Show, RenoShow::Hide));
  m.AddFilter(MakeFilter("OnlyNew", RenoShow::Hide, RenoShow::Hide, RenoShow::Show));
  m.AddFilter(MakeFilter("OnlyExisting", RenoShow::Show, RenoShow::Hide, RenoShow::Hide));
  m.SetViewFilter("demo list", "OnlyDemo"); m.SetViewFilter("new list", "OnlyNew"); m.SetViewFilter("existing list", "OnlyExisting");
  ScheduleDef d;
  Field q = F3("Quantity", 0); q.aggregate = Aggregate::Total;
  d.fields = {F3("Id"), F3("Status"), q};
  d.grandTotal = true;
  auto run = [&](const char* view) { return BuildSchedule(d, m.ScheduleRecords(view)); };
  ScheduleResult demo = run("demo list"), nw = run("new list"), ex = run("existing list"), all = run("all");
  d8test::Check(demo.rows.size() == 3 && demo.rows[0].cells[0] == "d1" && demo.rows[1].cells[1] == "Demolished" && demo.rows.back().cells[2] == "9", "renovation schedule: demolished list d1 d2 totals 9");
  d8test::Check(nw.rows.size() == 3 && nw.rows[0].cells[0] == "n1" && nw.rows.back().cells[2] == "13", "renovation schedule: new list n1 n2 totals 13");
  d8test::Check(ex.rows.back().cells[2] == "14" && all.rows.size() == 7 && all.rows.back().cells[2] == "36", "renovation schedule: existing 14, unfiltered 36 = 14 + 9 + 13");
  d8test::Check(m.ScheduleRecords("all", "Door").empty(), "renovation schedule: a type filter that matches nothing lists nothing");
}

// D8-ARCHICAD-RENO-0010: openings in a wall that changes status take the new status by default. Wall W1 with door D1 and window N1, wall W2
// with door D2, all New. Setting W1 to Demolished sets W1, D1, N1 (one element set directly) and leaves W2 and D2 New. With the option off
// only the wall changes. Setting the wall back to Existing carries its openings back.
D8_EXTRA_TEST(Rw4DocRenovationOpeningsFollowWall) {
  RenovationModel m;
  m.Add("W1", "Wall"); m.Add("D1", "Door", "W1"); m.Add("N1", "Window", "W1"); m.Add("W2", "Wall"); m.Add("D2", "Door", "W2");
  d8test::Check(m.SetStatus({"W1"}, RenoStatus::Demolished) == 1, "openings: one wall set directly");
  d8test::Check(m.Find("W1")->status == RenoStatus::Demolished && m.Find("D1")->status == RenoStatus::Demolished && m.Find("N1")->status == RenoStatus::Demolished, "openings: wall, door and window demolished");
  d8test::Check(m.Find("W2")->status == RenoStatus::New && m.Find("D2")->status == RenoStatus::New, "openings: the other wall and its door are unchanged");
  m.SetStatus({"W1"}, RenoStatus::Existing);
  d8test::Check(m.Find("D1")->status == RenoStatus::Existing && m.Find("N1")->status == RenoStatus::Existing, "openings: back to existing with the wall");
  m.SetStatus({"W2"}, RenoStatus::Demolished, false);
  d8test::Check(m.Find("W2")->status == RenoStatus::Demolished && m.Find("D2")->status == RenoStatus::New, "openings: with the option off only the wall changes");
}

// D8-ARCHICAD-RENO-0018 and D8-ARCHICAD-RENO-0026: one click sets the status of every selected element; the default status applies to
// elements placed later. Five elements a..e (default New). Selecting a, c, d (and an id that does not exist) and clicking Demolished sets
// exactly three. After setting the default to Existing, a new element f is Existing; after setting it back to New, g is New.
D8_EXTRA_TEST(Rw4DocRenovationSetStatusOnSelectionAndDefault) {
  RenovationModel m;
  for (const char* id : {"a", "b", "c", "d", "e"}) m.Add(id, "Wall");
  d8test::Check(m.SetStatus({"a", "c", "d", "zz"}, RenoStatus::Demolished) == 3, "status palette: three of four ids exist");
  std::string dem;
  for (auto& e : m.Elements()) if (e.status == RenoStatus::Demolished) dem += e.id;
  d8test::Check(dem == "acd" && m.Find("b")->status == RenoStatus::New && m.Find("e")->status == RenoStatus::New, "status palette: exactly the selection changed");
  m.defaultStatus = RenoStatus::Existing;
  m.Add("f", "Wall");
  d8test::Check(m.Find("f")->status == RenoStatus::Existing, "status palette: default Existing for a new element");
  m.defaultStatus = RenoStatus::New;
  m.Add("g", "Wall");
  d8test::Check(m.Find("g")->status == RenoStatus::New && m.Find("f")->status == RenoStatus::Existing, "status palette: default New later, earlier element untouched");
}

// D8-ARCHICAD-RENO-0021: an element restricted to one filter is visible only in it, provided its status would be shown there.
// Filters Plan (shows Existing and Demolished, hides New) and Demo (shows all). w1 Existing pinned to Plan: visible under Plan, hidden under
// Demo. w2 Demolished pinned to a filter "NoDemo" that hides Demolished: stays hidden there. w3 New, unpinned: visible under Demo only.
// Pinning to an unknown filter is refused.
D8_EXTRA_TEST(Rw4DocRenovationElementPinnedToOneFilter) {
  RenovationModel m;
  m.defaultStatus = RenoStatus::Existing; m.Add("w1", "Wall");
  m.defaultStatus = RenoStatus::Demolished; m.Add("w2", "Wall");
  m.defaultStatus = RenoStatus::New; m.Add("w3", "Wall");
  m.AddFilter(MakeFilter("Plan", RenoShow::Show, RenoShow::Show, RenoShow::Hide));
  m.AddFilter(MakeFilter("Demo", RenoShow::Show, RenoShow::Show, RenoShow::Show));
  m.AddFilter(MakeFilter("NoDemo", RenoShow::Show, RenoShow::Hide, RenoShow::Show));
  d8test::Check(m.Pin("w1", "Plan") && m.Pin("w2", "NoDemo") && !m.Pin("w3", "Missing"), "pinned filter: pins accepted, unknown filter refused");
  m.SetViewFilter("vPlan", "Plan"); m.SetViewFilter("vDemo", "Demo"); m.SetViewFilter("vNoDemo", "NoDemo");
  d8test::Check(Cat(m.VisibleIn("vPlan")) == "w1," && Cat(m.VisibleIn("vDemo")) == "w3,", "pinned filter: the Plan view shows only w1, the Demo view only w3");
  d8test::Check(m.LookInView("w1", "vPlan") == RenoLook::Normal && m.LookInView("w1", "vDemo") == RenoLook::Hidden, "pinned filter: w1 only under its filter");
  d8test::Check(m.LookInView("w2", "vNoDemo") == RenoLook::Hidden && m.LookInView("w2", "vDemo") == RenoLook::Hidden && m.LookInView("w2", "vPlan") == RenoLook::Hidden, "pinned filter: w2 hidden where its status is hidden and elsewhere because it is pinned");
  d8test::Check(m.LookInView("w3", "vDemo") == RenoLook::Normal && m.LookInView("w3", "vPlan") == RenoLook::Hidden, "pinned filter: the unpinned element follows the filters");
}

// D8-ARCHICAD-RENO-0022: reset at the end of a renovation. Elements a Existing, b Demolished, c New, d New pinned to "OldPlan", e Demolished
// pinned to "Final". Remap New -> Existing, Demolished -> delete, target filter "Final" (shows everything). a unchanged; b deleted; c becomes
// Existing (1 changed); d is pinned to another filter so it is hidden under Final: skipped, stays New; e is pinned to Final and visible: deleted.
// Counts: changed 1, deleted 2 (b, e), skipped 1 (d). Remaining a, c, d.
D8_EXTRA_TEST(Rw4DocRenovationResetRemapsStatuses) {
  RenovationModel m;
  m.defaultStatus = RenoStatus::Existing; m.Add("a", "Wall");
  m.defaultStatus = RenoStatus::Demolished; m.Add("b", "Wall");
  m.defaultStatus = RenoStatus::New; m.Add("c", "Wall"); m.Add("d", "Wall");
  m.defaultStatus = RenoStatus::Demolished; m.Add("e", "Wall");
  m.AddFilter(MakeFilter("OldPlan", RenoShow::Show, RenoShow::Show, RenoShow::Show));
  m.AddFilter(MakeFilter("Final", RenoShow::Show, RenoShow::Show, RenoShow::Show));
  m.Pin("d", "OldPlan"); m.Pin("e", "Final");
  RenoRemap map; map.to[(int)RenoStatus::New] = (int)RenoStatus::Existing; map.to[(int)RenoStatus::Demolished] = -1;
  RenoResetCounts c = m.Reset(map, "Final");
  d8test::Check(c.changed == 1 && c.deleted == 2 && c.skipped == 1, "reset: 1 changed, 2 deleted, 1 skipped");
  std::string left;
  for (auto& e : m.Elements()) left += e.id;
  d8test::Check(left == "acd" && m.Find("a")->status == RenoStatus::Existing && m.Find("c")->status == RenoStatus::Existing && m.Find("d")->status == RenoStatus::New, "reset: remaining elements and statuses");
}

// D8-ARCHICAD-RENO-0023: filters can be duplicated, renamed, deleted and exported or imported as XML. F1 (Existing show, Demolished override
// rule "A & B \"dash\"", New hide, annotations included). Duplicate F1 -> F2 has the same settings; a duplicate name is refused. Renaming F2 to
// F3 moves the view assignment and the pins that named it; deleting F3 clears the view's filter and the pin. XML export of F1 and F3 imports to
// an equal list (names, show settings, rules, annotation flag); a truncated file or an unknown setting is refused.
D8_EXTRA_TEST(Rw4DocRenovationFilterManagementAndXml) {
  RenovationModel m;
  m.Add("w", "Wall");
  RenoFilter f1 = MakeFilter("F1", RenoShow::Show, RenoShow::Override, RenoShow::Hide, false); f1.rule[1] = "A & B \"dash\"";
  d8test::Check(m.AddFilter(f1) && m.Duplicate("F1", "F2") && !m.Duplicate("F1", "F2") && !m.Duplicate("Nope", "F9"), "filter management: duplicate once");
  const RenoFilter* c = m.Filter("F2");
  d8test::Check(c && c->show[1] == RenoShow::Override && c->show[2] == RenoShow::Hide && c->rule[1] == f1.rule[1] && !c->excludeAnnotations, "filter management: the copy has the same settings");
  m.SetViewFilter("v", "F2"); m.Pin("w", "F2");
  d8test::Check(m.Rename("F2", "F3") && !m.Filter("F2") && m.ViewFilter("v") == "F3" && m.Find("w")->pinned == "F3" && !m.Rename("F3", "F1"), "filter management: rename follows the view and the pin, an existing name refused");
  std::string xml = ExportRenoFiltersXml(m.Filters());
  std::vector<RenoFilter> back;
  d8test::Check(ImportRenoFiltersXml(xml, back) && back.size() == 2, "filter xml: two filters come back");
  bool eq = back.size() == m.Filters().size();
  for (size_t i = 0; eq && i < back.size(); ++i) {
    auto& a = back[i]; auto& b = m.Filters()[i];
    eq = a.name == b.name && a.excludeAnnotations == b.excludeAnnotations;
    for (int k = 0; k < 3; ++k) eq = eq && a.show[k] == b.show[k] && a.rule[k] == b.rule[k];
  }
  d8test::Check(eq, "filter xml: round trip is equal, special characters included");
  d8test::Check(!ImportRenoFiltersXml(xml.substr(0, xml.size() - 25), back) && !ImportRenoFiltersXml("<RenovationFilters><Filter name=\"X\" existing=\"maybe\" demolished=\"show\" new=\"show\"/></RenovationFilters>", back), "filter xml: truncated or unknown setting refused");
  d8test::Check(m.Delete("F3") && m.ViewFilter("v").empty() && m.Find("w")->pinned.empty() && m.Filters().size() == 1 && !m.Delete("F3"), "filter management: delete clears the view and the pin");
}

// D8-ARCHICAD-SCHED-0004 and D8-ARCHICAD-SCHED-0036: schedule schemes as XML. A scheme with id S1, name 'Doors & Windows <A>', type Element,
// fields Mark, Width (2 decimals, total, scale 1000 mm), Area (formula Width*2.1), a percentage of Width by Mark, a combined size, a count; criteria
// Width >= 0.8 and Mark begins with D; sort by Mark (footer) then Width descending; collapsed rows with a grand total. Export, import and export
// again give the same text; the imported scheme builds the very same table as the original; id, name and type survive; broken input is refused.
D8_EXTRA_TEST(Rw4DocScheduleSchemeXmlRoundTrip) {
  ScheduleDef d;
  Field width = F3("Width", 2); width.aggregate = Aggregate::Total; width.scale = 1000; width.unit = " mm"; width.showUnit = true; width.totalPrefix = "\"= "; width.totalSuffix = "&";
  Field area; area.name = "Area"; area.kind = FieldKind::Formula; area.formula = "Width*2.1"; area.decimals = 2;
  Field share; share.name = "Share"; share.kind = FieldKind::Percent; share.percentOf = "Width"; share.percentBy = "Mark"; share.decimals = 1;
  Field size; size.name = "Size"; size.kind = FieldKind::Combined; size.separator = " x "; size.parts = {{"Width", "W", "", 1}, {"Height", "H", "m", 2}};
  Field cnt; cnt.name = "Count"; cnt.kind = FieldKind::Count;
  d.fields = {F3("Mark"), width, area, share, size, cnt};
  d.filters = {{"Width", Op::Ge, N3(0.8)}, {"Mark", Op::BeginsWith, S3("D")}};
  SortKey k1; k1.field = "Mark"; k1.footer = true;
  SortKey k2; k2.field = "Width"; k2.descending = true;
  d.sort = {k1, k2};
  d.itemize = false; d.grandTotal = true;
  std::vector<Record> recs = {R3({{"Mark", S3("D1")}, {"Width", N3(0.9)}, {"Height", N3(2.1)}}), R3({{"Mark", S3("D1")}, {"Width", N3(0.9)}, {"Height", N3(2.1)}}), R3({{"Mark", S3("D2")}, {"Width", N3(1.0)}, {"Height", N3(2.1)}}),
                              R3({{"Mark", S3("X1")}, {"Width", N3(1.2)}, {"Height", N3(2.4)}}), R3({{"Mark", S3("D3")}, {"Width", N3(0.7)}, {"Height", N3(2.1)}})};
  SchemeHeader h{"S1", "Doors & Windows <A>", "Element"};
  std::string xml = ExportSchemeXml(d, h);
  ScheduleDef back; SchemeHeader hb;
  d8test::Check(ImportSchemeXml(xml, back, hb) && hb.id == "S1" && hb.name == "Doors & Windows <A>" && hb.type == "Element", "scheme xml: header survives");
  d8test::Check(ExportSchemeXml(back, hb) == xml, "scheme xml: export of the import is the same text");
  ScheduleResult a = BuildSchedule(d, recs), b = BuildSchedule(back, recs);
  bool same = a.rows.size() == b.rows.size();
  for (size_t i = 0; same && i < a.rows.size(); ++i) same = a.rows[i].kind == b.rows[i].kind && a.rows[i].cells == b.rows[i].cells && a.rows[i].count == b.rows[i].count && a.rows[i].label == b.rows[i].label;
  d8test::Check(same && a.rows.size() == 5, "scheme xml: the imported scheme builds the same table (D1 x2 merged, D2, two footers, grand total)");
  // closed form of the table: D1 rows merge (count 2), width 900 mm; D2 1000 mm; X1 and D3 are filtered out; footer D1 total 1800 mm, D2 1000 mm; grand 2800 mm
  d8test::Check(a.rows[0].cells[5] == "2" && a.rows[0].cells[1] == "900.00 mm" && a.rows[1].cells[1] == "\"= 1800.00 mm&" && a.rows[3].cells[1] == "\"= 1000.00 mm&" && a.rows[4].cells[1] == "\"= 2800.00 mm&", "scheme xml: the table values follow the scheme");
  d8test::Check(!ImportSchemeXml(xml.substr(0, xml.size() - 12), back, hb) && !ImportSchemeXml("<Other/>", back, hb), "scheme xml: truncated or foreign input refused");
}

// D8-ARCHICAD-SCHED-0007: field catalog search and order. Catalog: Wall Thickness (Parameter), Width (Parameter), Door Width (Parameter),
// Fire Rating (Property), Uniclass (Classification), Leaf Width (Library part). "width" lists Width, Door Width, Leaf Width (catalog order);
// "door width" only Door Width; "WIDTH" the same as "width"; "property" lists Fire Rating (the group counts); "" lists all six; "xyz" none.
// Order [A,B,C]: B up -> B,A,C; A (now index 1) down -> B,C,A; up at the top and down at the bottom are refused.
D8_EXTRA_TEST(Rw4DocFieldCatalogSearchAndOrder) {
  std::vector<CatalogField> cat = {{"Wall Thickness", "Parameter"}, {"Width", "Parameter"}, {"Door Width", "Parameter"}, {"Fire Rating", "Property"}, {"Uniclass", "Classification"}, {"Leaf Width", "Library part"}};
  auto names = [&](const char* q) { std::string s; for (auto& f : SearchFields(cat, q)) s += f.name + ";"; return s; };
  d8test::Check(names("width") == "Width;Door Width;Leaf Width;" && names("WIDTH") == names("width"), "field search: 'width' in catalog order, case-insensitive");
  d8test::Check(names("door width") == "Door Width;" && names("property") == "Fire Rating;" && names("xyz").empty(), "field search: all words must match, the group is searched too");
  d8test::Check(SearchFields(cat, "").size() == 6 && SearchFields(cat, "   ").size() == 6, "field search: empty query lists all");
  std::vector<std::string> order = {"A", "B", "C"};
  d8test::Check(MoveField(order, 1, true) && Cat(order) == "B,A,C,", "field order: B up");
  d8test::Check(MoveField(order, 1, false) && Cat(order) == "B,C,A,", "field order: A down");
  d8test::Check(!MoveField(order, 0, true) && !MoveField(order, 2, false) && !MoveField(order, 5, true) && Cat(order) == "B,C,A,", "field order: the ends refuse");
}

// D8-ARCHICAD-SCHED-0008: main header, field headers, merged group headers. Five columns Mark Width Height Fire Cost, main header "Doors".
// Groups Size over columns 1..2 and Rating over column 3: rows: [Doors span 5]; [ "" 1, Size 2, Rating 1, "" 1 ]; [the five field headers].
// Hiding the main header or the field headers drops that row; no groups drops the group row; groups given out of order give the same row.
// Overlapping groups, a group outside the columns and an empty group are refused.
D8_EXTRA_TEST(Rw4DocScheduleHeaderRowsAndGroups) {
  std::vector<std::string> heads = {"Mark", "Width", "Height", "Fire", "Cost"};
  HeaderRows h = BuildHeaderRows("Doors", true, heads, true, {{"Size", 1, 2}, {"Rating", 3, 1}});
  d8test::Check(h.ok && h.rows.size() == 3 && h.rows[0].size() == 1 && h.rows[0][0].text == "Doors" && h.rows[0][0].span == 5, "headers: main header spans five columns");
  auto flat = [](const std::vector<HeaderCell>& r) { std::string s; for (auto& c : r) s += c.text + "/" + std::to_string(c.span) + ";"; return s; };
  d8test::Check(flat(h.rows[1]) == "/1;Size/2;Rating/1;/1;", "headers: group row with merged cells and gaps");
  d8test::Check(flat(h.rows[2]) == "Mark/1;Width/1;Height/1;Fire/1;Cost/1;", "headers: field header row");
  HeaderRows swapped = BuildHeaderRows("Doors", true, heads, true, {{"Rating", 3, 1}, {"Size", 1, 2}});
  d8test::Check(flat(swapped.rows[1]) == flat(h.rows[1]), "headers: groups in any order");
  d8test::Check(BuildHeaderRows("Doors", false, heads, true, {{"Size", 1, 2}}).rows.size() == 2 && BuildHeaderRows("Doors", true, heads, false, {}).rows.size() == 1 && BuildHeaderRows("", false, heads, false, {}).rows.empty(), "headers: hidden rows are dropped");
  d8test::Check(!BuildHeaderRows("x", true, heads, true, {{"A", 1, 2}, {"B", 2, 2}}).ok && !BuildHeaderRows("x", true, heads, true, {{"A", 4, 2}}).ok && !BuildHeaderRows("x", true, heads, true, {{"A", 1, 0}}).ok, "headers: overlap, out of range and empty groups refused");
  d8test::Check(BuildHeaderRows("x", true, heads, true, {{"A", 0, 5}}).ok && flat(BuildHeaderRows("x", false, heads, false, {{"A", 0, 5}}).rows[0]) == "A/5;", "headers: a group over all columns");
}

// D8-ARCHICAD-SCHED-0013 and D8-ARCHICAD-SCHED-0014: editable cells change the model at once; the schedule is refreshed on reopening;
// overridden values can be listed and restored. Doors d1 (Width 0.9) and d2 (1.0); Area is the formula Width*2.1 (1.89, 2.10). Editing d1's Width to
// 1.2 changes the model immediately; the open table still shows 1.89 until reopened, then 1.2 x 2.1 = 2.52. The calculated Area cell cannot
// be edited, nor can a non-editable field or a footer row. A model-side change of d2 to 0.8 shows after reopening as 1.68. Overridden('d1') =
// {Width}; Restore brings 0.9 back (Area 1.89 again) and the list empties; an unknown element cannot be restored.
D8_EXTRA_TEST(Rw4DocScheduleEditableCellsAndOverrides) {
  ScheduleDef def;
  Field area; area.name = "Area"; area.kind = FieldKind::Formula; area.formula = "Width*2.1"; area.decimals = 2;
  def.fields = {F3("Id"), F3("Width", 2), area};
  EditableSchedule s({R3({{"Id", S3("d1")}, {"Width", N3(0.9)}}), R3({{"Id", S3("d2")}, {"Width", N3(1.0)}})}, def, {"Width"});
  ScheduleResult open = s.Open();
  d8test::Check(open.rows[0].cells[2] == "1.89" && open.rows[1].cells[2] == "2.10", "editable cells: the table at first");
  d8test::Check(s.EditCell(open, 0, "Width", N3(1.2)) && Near3(s.Model()[0].at("Width").n, 1.2), "editable cells: the model changes at once");
  d8test::Check(open.rows[0].cells[2] == "1.89", "editable cells: the already open table is not redrawn");
  d8test::Check(s.Open().rows[0].cells[1] == "1.20" && s.Open().rows[0].cells[2] == "2.52", "editable cells: reopened it shows 1.2 and 2.52");
  d8test::Check(!s.EditCell(open, 0, "Area", N3(5)) && !s.EditCell(open, 0, "Id", S3("x")) && !s.EditCell(open, 9, "Width", N3(1)), "editable cells: calculated, locked and missing cells refused");
  s.ModelEdit("d2", "Width", N3(0.8));
  d8test::Check(s.Open().rows[1].cells[2] == "1.68", "editable cells: a model change shows after reopening (0.8 x 2.1 = 1.68)");
  d8test::Check(s.Overridden("d1") == std::vector<std::string>({"Width"}) && s.Overridden("d2") == std::vector<std::string>({"Width"}) && s.Overridden("zz").empty(), "overrides: the changed fields are listed");
  d8test::Check(s.Restore("d1", "Width") && s.Overridden("d1").empty() && s.Open().rows[0].cells[2] == "1.89" && !s.Restore("zz", "Width"), "overrides: restore gives back the default");
}

// D8-ARCHICAD-SCHED-0018: units. 1.2 m is 1200 mm, 120.0 cm, 1.20 m, 3.94 ft (1.2 / 0.3048 = 3.9370079), 47.244 in (1.2 / 0.0254 = 47.2440945)
// and 3' 11 1/4" (47.2440945 in x 16 = 755.9 -> 756 sixteenths = 47 1/4 in = 3 ft 11 1/4 in). Exact definitions: 0.3048 m = 1' 0", half an
// inch is 0' 0 1/2", -0.3048 m is -1' 0", 11.99 in rounds up through the carry to 1' 0". Areas: 0.09290304 m2 = 1 ft2 (= 1.000000), 2.5 m2 = 2500000
// mm2; volume 0.028316846592 m3 = 1 ft3. Editable dimensions use the dimension unit (1.236 m -> 1236 mm), calculated values the
// calculation unit (1.236 m -> 1.24 m).
D8_EXTRA_TEST(Rw4DocUnitFormats) {
  d8test::Check(FormatLength(1.2, LengthUnit::Millimeter, 0) == "1200 mm" && FormatLength(1.2, LengthUnit::Centimeter, 1) == "120.0 cm" && FormatLength(1.2, LengthUnit::Meter, 2) == "1.20 m", "units: metric lengths");
  d8test::Check(FormatLength(1.2, LengthUnit::DecimalFoot, 2) == "3.94 ft" && FormatLength(1.2, LengthUnit::DecimalInch, 3) == "47.244 in", "units: decimal feet and inches");
  d8test::Check(FormatLength(1.2, LengthUnit::FeetFractionalInch, 0) == "3' 11 1/4\"", "units: feet and fractional inches");
  d8test::Check(FormatLength(0.3048, LengthUnit::FeetFractionalInch, 0) == "1' 0\"" && FormatLength(0.0127, LengthUnit::FeetFractionalInch, 0) == "0' 0 1/2\"" && FormatLength(-0.3048, LengthUnit::FeetFractionalInch, 0) == "-1' 0\"" &&
                    FormatLength(11.99 * 0.0254, LengthUnit::FeetFractionalInch, 0) == "1' 0\"", "units: exact foot, half inch, negative, carry");
  d8test::Check(FormatLength(0.0254 * 5.5, LengthUnit::FeetFractionalInch, 0, 2) == "0' 5 1/2\"" && FormatLength(0.0254 * 5.25, LengthUnit::FeetFractionalInch, 0, 4) == "0' 5 1/4\"", "units: coarser denominators");
  d8test::Check(FormatArea(0.09290304, AreaUnit::SquareFoot, 6) == "1.000000 ft2" && FormatArea(2.5, AreaUnit::SquareMillimeter, 0) == "2500000 mm2" && FormatArea(2.5, AreaUnit::SquareMeter, 2) == "2.50 m2", "units: areas");
  d8test::Check(FormatVolume(0.028316846592, VolumeUnit::CubicFoot, 6) == "1.000000 ft3" && FormatVolume(2, VolumeUnit::CubicMeter, 1) == "2.0 m3", "units: volumes");
  UnitSettings us;
  d8test::Check(FormatByKind(1.236, true, us) == "1236 mm" && FormatByKind(1.236, false, us) == "1.24 m", "units: editable dimension versus calculated value");
  us.dimension = LengthUnit::FeetFractionalInch; us.calculation = LengthUnit::DecimalFoot; us.calcDecimals = 3;
  d8test::Check(FormatByKind(1.2, true, us) == "3' 11 1/4\"" && FormatByKind(1.2, false, us) == "3.937 ft", "units: the two settings are independent");
}

// D8-ARCHICAD-SCHED-0025: expression-defined properties as fields. Doors (Width, Height, Rate): (0.9, 2.1, 100) and (1.0, 2.1, 100) and one
// without Rate. Area = Width*Height (1.89, 2.1, 1.89); Cost = Area*Rate declared BEFORE Area in the field list (189, 210, not evaluated for the
// door without Rate); Big = if(Area>2,1,0) (0, 1, 0). Properties that depend on themselves or on each other (Loop1 = Loop2+1, Loop2 =
// Loop1+1, Self = Self+1) are not evaluated. A syntax error is reported by the parser.
D8_EXTRA_TEST(Rw4DocScheduleExpressionProperties) {
  ScheduleDef d;
  auto expr = [](const char* n, const char* e, int dec) { Field f; f.name = n; f.kind = FieldKind::Formula; f.formula = e; f.decimals = dec; return f; };
  d.fields = {expr("Cost", "Area*Rate", 2), expr("Area", "Width*Height", 2), expr("Big", "if(Area>2,1,0)", 0), expr("Loop1", "Loop2+1", 0), expr("Loop2", "Loop1+1", 0), expr("Self", "Self+1", 0)};
  std::vector<Record> recs = {R3({{"Width", N3(0.9)}, {"Height", N3(2.1)}, {"Rate", N3(100)}}), R3({{"Width", N3(1.0)}, {"Height", N3(2.1)}, {"Rate", N3(100)}}), R3({{"Width", N3(0.9)}, {"Height", N3(2.1)}})};
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(r.rows[0].cells == std::vector<std::string>({"189.00", "1.89", "0", "-", "-", "-"}), "expression properties: first door, chain declared out of order");
  d8test::Check(r.rows[1].cells == std::vector<std::string>({"210.00", "2.10", "1", "-", "-", "-"}), "expression properties: second door");
  d8test::Check(r.rows[2].cells == std::vector<std::string>({"-", "1.89", "0", "-", "-", "-"}), "expression properties: a missing input leaves the dependent value not evaluated");
  Value v;
  Record none;
  d8test::Check(!Formula::Eval("Width*(", none, v) && !Formula::Eval("Width Height", none, v) && Formula::Eval("Width*Height", none, v) && !v.Defined(), "expression properties: syntax errors versus unknown inputs");
}

// D8-ARCHICAD-SCHED-0027: export property values for consultants, preview and import the revised values. Elements E1 (FireRating FD30, Cost 100),
// E2 (FD60, 200), E3 (no rating, 300), E4 (FD30, 400). Export of [FireRating, Cost]: header Id FireRating Cost; E3's rating cell is empty.
// The consultant returns: E1 FireRating FD60 (changed) Cost 100 (same); E2 Cost 250.5; E3 FireRating FD30; E4 Cost 'abc'; and an unknown E9.
// Preview: changes E1 FireRating FD30->FD60, E2 Cost 200->250.5, E3 FireRating ''->FD30; invalid E4/Cost; unmatched E9. With the property filter
// {FireRating}: only the E1 and E3 changes (E4 Cost is not looked at, so nothing is invalid). Applying the full preview makes E2 Cost 250.5 (a
// number) and E1 FD60.
D8_EXTRA_TEST(Rw4DocScheduleExportAndImportPropertyValues) {
  std::vector<Record> model = {R3({{"Id", S3("E1")}, {"FireRating", S3("FD30")}, {"Cost", N3(100)}}), R3({{"Id", S3("E2")}, {"FireRating", S3("FD60")}, {"Cost", N3(200)}}),
                               R3({{"Id", S3("E3")}, {"Cost", N3(300)}}), R3({{"Id", S3("E4")}, {"FireRating", S3("FD30")}, {"Cost", N3(400)}})};
  Sheet out = ExportPropertySheet(model, "Id", {"FireRating", "Cost"});
  d8test::Check(out.size() == 5 && out[0] == std::vector<std::string>({"Id", "FireRating", "Cost"}) && out[3] == std::vector<std::string>({"E3", "", "300"}) && out[4][2] == "400", "property sheet: export rows");
  Sheet back = {{"Id", "FireRating", "Cost"}, {"E1", "FD60", "100"}, {"E2", "FD60", "250.5"}, {"E3", "FD30", "300"}, {"E4", "FD30", "abc"}, {"E9", "FD30", "1"}};
  ImportPreview p = PreviewImport(model, "Id", back, {});
  d8test::Check(p.changes.size() == 3 && p.changes[0].id == "E1" && p.changes[0].prop == "FireRating" && p.changes[0].oldText == "FD30" && p.changes[0].newText == "FD60", "property sheet: first change E1 FireRating");
  d8test::Check(p.changes[1].id == "E2" && p.changes[1].prop == "Cost" && p.changes[1].oldText == "200" && p.changes[1].newText == "250.5" && p.changes[2].id == "E3" && p.changes[2].oldText.empty() && p.changes[2].newText == "FD30", "property sheet: E2 cost and E3 rating");
  d8test::Check(p.invalid == std::vector<std::string>({"E4/Cost"}) && p.unmatched == std::vector<std::string>({"E9"}), "property sheet: invalid number and unknown element reported");
  ImportPreview f = PreviewImport(model, "Id", back, {"FireRating"});
  d8test::Check(f.changes.size() == 2 && f.changes[0].id == "E1" && f.changes[1].id == "E3" && f.invalid.empty() && f.unmatched.size() == 1, "property sheet: the property filter limits the changes");
  ApplyImport(model, "Id", p.changes);
  d8test::Check(model[1]["Cost"].kind == Value::Num && Near3(model[1]["Cost"].n, 250.5) && model[0]["FireRating"].s == "FD60" && model[2]["FireRating"].s == "FD30" && Near3(model[3]["Cost"].n, 400), "property sheet: applied values, number stays a number, rejected cell untouched");
  d8test::Check(PreviewImport(model, "Id", back, {}).changes.empty(), "property sheet: after the import nothing is left to change");
}

// D8-ARCHICAD-SCHED-0030: headline row per element. Doors D1 (0.9, FD30) and D2 (1.0, FD60), fields Mark Width Fire. Headline of Mark and
// Fire with separator " / ", header prefixes and one gap row: "Mark: D1 / Fire: FD30", the data row "D1<TAB>0.9<TAB>FD30", a blank line, then the same
// for D2. Without prefixes: "D1 / FD30". A field name that is not in the schedule is skipped. Gap 0 leaves no blank lines.
D8_EXTRA_TEST(Rw4DocScheduleHeadlineRows) {
  ScheduleDef d;
  d.fields = {F3("Mark"), F3("Width", 1), F3("Fire")};
  ScheduleResult r = BuildSchedule(d, {R3({{"Mark", S3("D1")}, {"Width", N3(0.9)}, {"Fire", S3("FD30")}}), R3({{"Mark", S3("D2")}, {"Width", N3(1.0)}, {"Fire", S3("FD60")}})});
  HeadlineOptions o; o.fields = {"Mark", "Fire"}; o.separator = " / "; o.headerPrefix = true; o.gapRows = 1;
  std::vector<std::string> lines = RenderWithHeadline(d, r, o);
  d8test::Check(lines == std::vector<std::string>({"Mark: D1 / Fire: FD30", "D1\t0.9\tFD30", "", "Mark: D2 / Fire: FD60", "D2\t1.0\tFD60", ""}), "headline: prefixes, separator, gap row");
  o.headerPrefix = false; o.gapRows = 0; o.fields = {"Mark", "Nope", "Fire"};
  lines = RenderWithHeadline(d, r, o);
  d8test::Check(lines == std::vector<std::string>({"D1 / FD30", "D1\t0.9\tFD30", "D2 / FD60", "D2\t1.0\tFD60"}), "headline: no prefix, unknown field skipped, no gap");
}

// D8-ARCHICAD-SCHED-0033 and D8-REVIT-SCHED-0013: an oversized schedule is split into parts. 23 rows at 10 per part: parts (0,10) (10,10) (20,3);
// 61 rows: six parts of 10 and one of 1, the parts are contiguous and add up to 61. Rows that fit a sheet: height 594, title 20, header 10, row 8:
// (594 - 30) / 8 = 70.5 -> 70 rows; a height that leaves less than one row gives 0. Side by side: 7 parts of width 180 with gap 10 on a sheet 841 wide:
// floor(851 / 190) = 4 per layout -> layouts of 4 and 3 parts; 4 parts need one layout; a part wider than the sheet gives none.
D8_EXTRA_TEST(Rw4DocScheduleSplitIntoParts) {
  auto parts = SplitRows(23, 10);
  d8test::Check(parts.size() == 3 && parts[0].first == 0 && parts[0].count == 10 && parts[1].first == 10 && parts[1].count == 10 && parts[2].first == 20 && parts[2].count == 3, "split: 23 rows at 10 -> 10, 10, 3");
  parts = SplitRows(61, 10);
  int sum = 0, expectFirst = 0;
  bool contiguous = true;
  for (auto& p : parts) { contiguous = contiguous && p.first == expectFirst; expectFirst += p.count; sum += p.count; }
  d8test::Check(parts.size() == 7 && parts[6].count == 1 && sum == 61 && contiguous, "split: 61 rows give seven contiguous parts adding up to 61");
  d8test::Check(SplitRows(0, 10).empty() && SplitRows(5, 0).empty() && SplitRows(5, 10).size() == 1 && SplitRows(10, 10).size() == 1, "split: edge cases");
  d8test::Check(RowsThatFit(594, 20, 10, 8) == 70 && RowsThatFit(30, 20, 10, 8) == 0 && RowsThatFit(38, 20, 10, 8) == 1 && RowsThatFit(594, 20, 10, 0) == 0, "split: rows that fit a sheet height");
  auto lay = ArrangeParts(7, 180, 10, 841);
  d8test::Check(lay == std::vector<int>({4, 3}) && ArrangeParts(4, 180, 10, 841) == std::vector<int>({4}) && ArrangeParts(5, 180, 10, 841) == std::vector<int>({4, 1}) && ArrangeParts(3, 900, 10, 841).empty(), "split: parts per layout");
}

// D8-REVIT-SCHED-0022: label from parameters. Parts: Mark (prefix "Door ", one space after), Width (suffix " mm"). Values Mark 12, Width 900:
// "Door 12 900 mm". An empty Width drops the whole part (no " mm"): "Door 12". With the sample value 000 standing in: "Door 12 000 mm".
// A forced break after Mark gives two lines "Door 12" and "900 mm". Wrapping at 12 columns between parameters only: Alpha(1) Bravo(1) Charlie ->
// "Alpha Bravo" / "Charlie". A long piece "very long label text" in 10 columns: kept whole when wrapping is between parameters only, cut at its spaces
// otherwise: "very long" / "label text". The drop of an empty parameter's prefix and suffix is an assumption.
D8_EXTRA_TEST(Rw4DocTagLabelBuilding) {
  std::vector<LabelPart> parts(2);
  parts[0].param = "Mark"; parts[0].prefix = "Door "; parts[0].spacesAfter = 1;
  parts[1].param = "Width"; parts[1].suffix = " mm"; parts[1].sample = "000";
  std::map<std::string, std::string> v = {{"Mark", "12"}, {"Width", "900"}};
  d8test::Check(BuildLabel(parts, v) == std::vector<std::string>({"Door 12 900 mm"}), "label: prefix, space, suffix");
  v["Width"] = "";
  d8test::Check(BuildLabel(parts, v) == std::vector<std::string>({"Door 12"}), "label: an empty parameter drops its text");
  d8test::Check(BuildLabel(parts, v, true) == std::vector<std::string>({"Door 12 000 mm"}), "label: the sample value stands in");
  v["Width"] = "900"; parts[0].breakAfter = true;
  d8test::Check(BuildLabel(parts, v) == std::vector<std::string>({"Door 12", "900 mm"}), "label: forced line break");
  std::vector<LabelPart> w(3);
  w[0].param = "A"; w[0].spacesAfter = 1; w[1].param = "B"; w[1].spacesAfter = 1; w[2].param = "C";
  std::map<std::string, std::string> wv = {{"A", "Alpha"}, {"B", "Bravo"}, {"C", "Charlie"}};
  d8test::Check(BuildLabel(w, wv, false, 12) == std::vector<std::string>({"Alpha Bravo", "Charlie"}), "label: wrap between parameters");
  d8test::Check(BuildLabel(w, wv, false, 0) == std::vector<std::string>({"Alpha Bravo Charlie"}), "label: no wrap width");
  std::vector<LabelPart> lp(1); lp[0].param = "L";
  std::map<std::string, std::string> lv = {{"L", "very long label text"}};
  d8test::Check(BuildLabel(lp, lv, false, 10, true) == std::vector<std::string>({"very long label text"}), "label: between-parameters-only keeps a long piece whole");
  d8test::Check(BuildLabel(lp, lv, false, 10, false) == std::vector<std::string>({"very long", "label text"}), "label: otherwise it is cut at spaces");
}

// D8-REVIT-SCHED-0006: formatting: hidden columns, units and conditional formatting. Doors (Mark, Width, Fire): (D1, 0.9, FD30) (D2, 1.1, FD60) (D3, 0.9, FD60)
// (D4, 0.9, no rating). Width is hidden: the cells carry Mark and Fire only and the headings omit Width. Width shown in mm with no decimals
// reads "900 mm". Rules in order: Width > 1.0 -> red; Fire = FD60 -> green; Fire has no value -> grey. D1 none, D2 red (first rule wins, an assumption),
// D3 green, D4 grey. Alignment and heading text formats are not modelled.
D8_EXTRA_TEST(Rw4DocScheduleFormatHiddenUnitsConditional) {
  std::vector<Record> recs = {R3({{"Mark", S3("D1")}, {"Width", N3(0.9)}, {"Fire", S3("FD30")}}), R3({{"Mark", S3("D2")}, {"Width", N3(1.1)}, {"Fire", S3("FD60")}}),
                              R3({{"Mark", S3("D3")}, {"Width", N3(0.9)}, {"Fire", S3("FD60")}}), R3({{"Mark", S3("D4")}, {"Width", N3(0.9)}})};
  ScheduleDef d;
  Field w = F3("Width", 0); w.hidden = true;
  d.fields = {F3("Mark"), w, F3("Fire")};
  ScheduleResult r = BuildSchedule(d, recs);
  d8test::Check(Headings(d) == std::vector<std::string>({"Mark", "Fire"}) && r.rows[0].cells == std::vector<std::string>({"D1", "FD30"}) && r.rows[3].cells == std::vector<std::string>({"D4", "-"}), "format: a hidden column is left out of headings and cells");
  d.fields[1].hidden = false; d.fields[1].scale = 1000; d.fields[1].unit = " mm"; d.fields[1].showUnit = true;
  d8test::Check(BuildSchedule(d, recs).rows[0].cells[1] == "900 mm" && BuildSchedule(d, recs).rows[1].cells[1] == "1100 mm", "format: unit format 0.9 m -> 900 mm");
  std::vector<CondFormat> rules = {{{"Width", Op::Gt, N3(1.0)}, "red"}, {{"Fire", Op::Eq, S3("FD60")}, "green"}, {{"Fire", Op::HasNoValue, Value()}, "grey"}};
  d8test::Check(ConditionalBackground(rules, recs[0]).empty() && ConditionalBackground(rules, recs[1]) == "red" && ConditionalBackground(rules, recs[2]) == "green" && ConditionalBackground(rules, recs[3]) == "grey", "format: conditional backgrounds");
  d8test::Check(ConditionalBackground({}, recs[0]).empty(), "format: no rules, no colour");
}

}  // namespace
