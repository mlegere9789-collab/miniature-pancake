// Unit test for named, reusable plot style tables (doc/Document.h's
// PlotStyleEntry/PlotStyleTable/FindPlotStyleEntry and their `style`
// parameter on EffectivePrintWidthMm/EffectivePlotColor/LayerPrints, plus
// doc/PlotStyleTables.h's Load/Save/Active-name persistence) - the real
// CTB/STB-style table PARITY_MAP.md's "Print and plot output" item still
// named missing even after Layer::print_width_mm/plot_color (tests/
// test_print_width.cpp, tests/test_plot_color.cpp) closed the per-layer
// lineweight/color halves: a document can now define "Monochrome" once
// and assign it to several layers, or swap between tables, instead of
// only ever one flat color/width pair per layer.
#include <cmath>
#include <cstdio>

#include "doc/Document.h"
#include "doc/PlotStyleTables.h"

using dino8::app::ActivePlotStyleTable;
using dino8::app::ActivePlotStyleTableName;
using dino8::app::Color;
using dino8::app::Document;
using dino8::app::EffectivePlotColor;
using dino8::app::EffectivePrintWidthMm;
using dino8::app::FindPlotStyleEntry;
using dino8::app::FindPlotStyleTable;
using dino8::app::Layer;
using dino8::app::LayerPrints;
using dino8::app::LoadPlotStyleTables;
using dino8::app::PlotStyleEntry;
using dino8::app::PlotStyleTable;
using dino8::app::SavePlotStyleTables;
using dino8::app::SetActivePlotStyleTableName;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
bool SameColor(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
}  // namespace

int main() {
  const Color display = Color::FromBytes(40, 180, 60);  // some on-screen green
  const Color black = Color::FromBytes(0, 0, 0);

  // --- Header-only layer: FindPlotStyleEntry / the `style`-aware overloads.
  {
    PlotStyleTable table;
    table.name = "Monochrome";
    PlotStyleEntry e;
    e.layer = "Walls";
    e.has_color = true;
    e.color = black;
    e.width_mm = 0.5;
    table.entries.push_back(e);
    PlotStyleEntry e2;
    e2.layer = "Doors";
    e2.width_mm = -1;  // "does not print", same convention print_width_mm itself uses
    table.entries.push_back(e2);

    Check(FindPlotStyleEntry(&table, "Walls") != nullptr, "FindPlotStyleEntry finds a row by exact layer name");
    Check(FindPlotStyleEntry(&table, "Nope") == nullptr, "...and returns null for a layer with no row");
    Check(FindPlotStyleEntry(nullptr, "Walls") == nullptr, "...and returns null when there is no table at all");

    Layer layer;
    layer.name = "Walls";
    const PlotStyleEntry* style = FindPlotStyleEntry(&table, layer.name);
    Check(SameColor(EffectivePlotColor(layer, display, style), black), "the table's own color wins over both the layer's plot_color and its display color");
    Check(SameColor(EffectivePlotColor(layer, display, nullptr), display), "...but only when a style entry is actually passed - no entry, no override (same as before this feature existed)");
    Check(EffectivePrintWidthMm(layer, 0.25, style) == 0.5, "the table's own width wins over the document default");
    Check(LayerPrints(layer, style), "a positive table width still means 'prints'");

    Layer doors;
    doors.name = "Doors";
    const PlotStyleEntry* doors_style = FindPlotStyleEntry(&table, doors.name);
    Check(!LayerPrints(doors, doors_style), "a negative table width means 'does not print', same convention Layer::print_width_mm itself uses");

    // The table's entry is the authority once a layer is assigned to one;
    // the layer's OWN plot_color/print_width_mm are irrelevant when an
    // entry exists for it, exactly the way a real CTB/STB table overrides
    // whatever a layer's own properties say once it's plotted through it.
    Layer with_own_override;
    with_own_override.name = "Walls";
    with_own_override.has_plot_color = true;
    with_own_override.plot_color = Color::FromBytes(255, 0, 0);  // a totally different color
    with_own_override.print_width_mm = 2.0;
    Check(SameColor(EffectivePlotColor(with_own_override, display, style), black), "the table's color still wins even over the layer's own plot_color override");
    Check(EffectivePrintWidthMm(with_own_override, 0.25, style) == 0.5, "the table's width still wins even over the layer's own print_width_mm override");
  }

  // --- Document-level persistence: LoadPlotStyleTables/SavePlotStyleTables
  // round-trip through UserText() JSON, and the active-table name survives
  // independently of the table list, mirroring BlockInstances.h's own
  // "small side table as document user text" persistence shape.
  Document doc;
  Check(LoadPlotStyleTables(doc).empty(), "a freshly constructed Document has no plot style tables");
  Check(ActivePlotStyleTableName(doc).empty(), "...and no active table");

  std::vector<PlotStyleTable> tables;
  {
    PlotStyleTable mono;
    mono.name = "Monochrome";
    PlotStyleEntry e;
    e.layer = "Walls";
    e.has_color = true;
    e.color = black;
    e.width_mm = 0.5;
    mono.entries.push_back(e);
    tables.push_back(mono);
  }
  SavePlotStyleTables(doc, tables);
  {
    std::vector<PlotStyleTable> reloaded = LoadPlotStyleTables(doc);
    Check(reloaded.size() == 1, "one table round-trips through SavePlotStyleTables/LoadPlotStyleTables");
    if (reloaded.size() == 1) {
      Check(reloaded[0].name == "Monochrome", "the table's name survives");
      Check(reloaded[0].entries.size() == 1, "the table's one entry survives");
      if (reloaded[0].entries.size() == 1) {
        Check(reloaded[0].entries[0].layer == "Walls", "the entry's layer name survives");
        Check(reloaded[0].entries[0].has_color, "the entry's has_color flag survives");
        Check(SameColor(reloaded[0].entries[0].color, black), "the entry's color survives");
        Check(reloaded[0].entries[0].width_mm == 0.5, "the entry's width survives");
      }
    }
  }

  SetActivePlotStyleTableName(doc, "Monochrome");
  Check(ActivePlotStyleTableName(doc) == "Monochrome", "the active table name is now set");
  {
    std::vector<PlotStyleTable> still_there = LoadPlotStyleTables(doc);
    Check(still_there.size() == 1, "setting the active table name did not drop the table list it lives alongside");
  }

  {
    std::vector<PlotStyleTable> storage;
    const PlotStyleTable* active = ActivePlotStyleTable(doc, storage);
    Check(active != nullptr && active->name == "Monochrome", "ActivePlotStyleTable resolves the active name against the loaded tables");
  }

  SetActivePlotStyleTableName(doc, "");
  Check(ActivePlotStyleTableName(doc).empty(), "clearing the active name (PlotStyleTableActivate None) deactivates it");
  {
    std::vector<PlotStyleTable> storage;
    Check(ActivePlotStyleTable(doc, storage) == nullptr, "...and ActivePlotStyleTable now resolves to null, falling straight back to each layer's own fields");
    Check(LoadPlotStyleTables(doc).size() == 1, "...but the table itself is still there, just not active");
  }

  Check(FindPlotStyleTable(tables, "Nope") == nullptr, "FindPlotStyleTable returns null for an unknown name");
  Check(FindPlotStyleTable(tables, "") == nullptr, "...and for an empty name (never matches - that's the 'no active table' sentinel, not a real table name)");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
