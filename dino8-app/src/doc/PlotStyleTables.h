// Named, reusable plot style tables (PARITY_MAP.md's "Print and plot
// output" item: the CTB/STB half Layer::print_width_mm/plot_color alone -
// real but only ever one flat pair of fields per layer - never closed).
//
// A document's whole set of PlotStyleTable (doc/Document.h) rows, plus
// which one is currently active, round-trips through
// Document::UserText()["dino8.plot_style_tables"] as one JSON object -
// the same "small side table persisted as document user text" shape
// BlockInstances.h already established for dynamic-block instance
// records, so it survives Save/Open and (once BeginChange's own capture
// of UserText() is correct - see Document.h's Snapshot::user_text
// comment) Undo/Redo for free.
#pragma once

#include <string>
#include <vector>

#include "doc/Document.h"

namespace dino8::app {

std::vector<PlotStyleTable> LoadPlotStyleTables(const Document& doc);
void SavePlotStyleTables(Document& doc, const std::vector<PlotStyleTable>& tables);

// The name of the document's currently active plot style table ("" if
// none is active - Print/Export then falls straight back to each layer's
// own print_width_mm/plot_color, exactly as before this feature existed).
std::string ActivePlotStyleTableName(const Document& doc);
void SetActivePlotStyleTableName(Document& doc, const std::string& name);

// Finds `name` in `tables` ("" never matches - ActivePlotStyleTableName()
// returning "" means "no active table", not a table literally named "").
const PlotStyleTable* FindPlotStyleTable(const std::vector<PlotStyleTable>& tables, const std::string& name);

// Convenience for a call site (Print/Export, a command) that just wants
// "whichever table is active right now, or null": loads the document's
// tables, resolves ActivePlotStyleTableName() against them, and returns a
// pointer into `storage` (which the caller must keep alive at least as
// long as the returned pointer is used) so the result survives past this
// function's own temporary LoadPlotStyleTables() call.
const PlotStyleTable* ActivePlotStyleTable(const Document& doc, std::vector<PlotStyleTable>& storage);

}  // namespace dino8::app
