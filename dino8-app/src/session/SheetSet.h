// Sheet sets: a named, ordered list of paper-space layout sheets spanning
// *multiple* .3dm files (AutoCAD's Sheet Set Manager), kept as a plain-text
// JSON file (Dino 8's own convention - see session/Worksession.h's .rws
// files for the same idea applied to attached reference models).
//
// A Dino 8 document already has its own Layout tabs (paper-space sheets
// within one file - see doc/Document.h's Layout/LayoutDetail), but nothing
// ties layouts from *different* files together into one named set the way
// AutoCAD's sheet sets do. This gives Dino 8 that cross-file list, plus a
// batch "plot the whole set to PDF" operation, without inventing a live
// cloud-hosted sheet set (out of scope - see PlotSheetSet's comment).
#pragma once

#include <string>
#include <vector>

namespace dino8::app {

// One sheet: a specific Layout, named by its Document::Layout::name, inside
// a specific .3dm file. `file` is stored exactly as given (relative paths
// resolve against the current working directory when the set is plotted,
// same as every other file path this app takes on the command line).
struct SheetSetEntry {
  std::string file;
  std::string layout;
};

struct SheetSetFile {
  std::string name;
  std::vector<SheetSetEntry> entries;
};

// Creates a new, empty sheet set file named `name` at `path`. Overwrites an
// existing file at `path` (SheetSetNew is "start a fresh set", not merge).
bool CreateSheetSet(const std::string& path, const std::string& name, std::string& error);

// Loads a sheet set file written by CreateSheetSet/SaveSheetSet.
bool LoadSheetSet(const std::string& path, SheetSetFile& out, std::string& error);

// Writes `set` to `path` in the same JSON shape LoadSheetSet reads.
bool SaveSheetSet(const std::string& path, const SheetSetFile& set, std::string& error);

// Loads the sheet set at `path` (which must already exist - use
// SheetSetNew first), appends {file, layout} unless that exact pair is
// already present, and saves it back. Returns false (with `error` set) if
// the file can't be loaded or written.
bool AddSheetSetEntry(const std::string& path, const std::string& file, const std::string& layout,
                       std::string& error);

// One sheet's plot outcome, in entry order.
struct SheetPlotResult {
  std::string file;
  std::string layout;
  std::string output_path;
  bool ok = false;
  std::string error;  // set when !ok
};

// Batch-plots every entry in `set` to its own PDF in `out_dir` (created if
// missing): one file per referenced file+layout, named
// "<file-stem>__<layout>.pdf" so entries from different files never collide.
//
// Each entry's source file is loaded into its own throwaway Document (the
// same read-only load Worksession::AttachWorksession uses for reference
// models) - the currently-open/active document is never touched. The
// referenced Layout's page size (width_mm/height_mm) drives the PDF's page;
// this reuses FileExchange.cpp's existing ExportPdf projector rather than a
// second PDF writer, so plotted geometry is model-space content fitted to
// that layout's sheet size, not a full render of the layout's own details -
// Dino 8 has no paper-space detail compositor to reuse here (see
// FileExchange.cpp's ExportPdf/PrepareDrawing), which is an honest scope
// limit worth calling out rather than a full paper-space renderer.
//
// One PDF per sheet, not one combined multi-page PDF: FileExchange.cpp's
// PDF writer emits a single-page xref/trailer per call, with no multi-page
// object graph to extend here, so merging would mean writing a second PDF
// path in this change - out of scope for this increment.
//
// Returns one SheetPlotResult per entry (success or failure); a missing
// file or missing layout name is recorded as a per-entry failure rather
// than aborting the whole batch.
std::vector<SheetPlotResult> PlotSheetSet(const SheetSetFile& set, const std::string& out_dir);

}  // namespace dino8::app
