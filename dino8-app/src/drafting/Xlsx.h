// A minimal, real, dependency-free .xlsx (OOXML SpreadsheetML) reader and
// writer for DataLink/DataLinkUpdate (cmd_drafting2.cpp), closing this app's
// "not native .xlsx" gap alongside its existing CSV two-way sync - see
// PARITY_MAP.md's "Live external data linking into tables" entry.
//
// Deliberately narrow, like every other hand-rolled format codec in this
// app (the Radiance .hdr/RGBE codec, render/ImageIO.cpp; the ZIP-based mesh
// formats, geom/*.cpp): one sheet, one cell type. Every cell is written as
// a plain string (t="inlineStr"), matching TableSpec's own bare-string
// model (drafting/Table.h - "no formulas, no styles, no merges") exactly
// the same representation choice WriteCsvFile/CsvField already make for
// CSV - so a cell like "007" or "3,5" round-trips byte-for-byte instead of
// silently becoming a typed number and losing its exact text. No styles,
// merged cells or multiple sheets are written or read.
//
// The writer emits every zip entry uncompressed (method 0 "store") - a
// fully valid zip (the format never requires compression), openable by any
// real spreadsheet program exactly like a deflated one. The reader accepts
// both store and deflate (method 8, via util/Inflate.h's raw RFC 1951
// decoder) - a real Excel/LibreOffice/openpyxl save always deflates - and
// resolves the workbook's first sheet through its own relationship graph
// (workbook.xml's <sheet r:id=.../> -> workbook.xml.rels' Target), not a
// "sheet1.xml" filename guess, plus a shared-strings table
// (xl/sharedStrings.xml) when present, so a file actually round-tripped
// through a real spreadsheet program - not just this app's own writer -
// reads back correctly too.
#pragma once

#include <string>
#include <vector>

namespace dino8::app::drafting {

// Writes a `rows` x `cols` row-major grid of cell text (`cells[r*cols+c]`,
// same shape as TableSpec::cells) as a new one-sheet .xlsx at `path`,
// overwriting any existing file. An empty cell is simply omitted from the
// sheet XML (standard sparse-cell practice), not written as an empty
// string cell. Returns false (with `error` set) on an empty grid or an I/O
// failure.
bool WriteXlsxCells(const std::string& path, int rows, int cols, const std::vector<std::string>& cells, std::string& error);

// Reads a .xlsx workbook's first worksheet back into a row-major grid of
// cell text (one inner vector per row, individually only as wide as that
// row's last non-empty cell - same ragged shape ReadCsvFile's rows use, and
// consumed the same way by cmd_drafting2.cpp's ApplyCsvToSpec). A numeric,
// shared-string, inline-string or formula cell all come back as their
// displayed/cached text - a formula's live expression is never available
// (no formula engine here), only whatever value the writing program last
// computed and stored, the exact same "frozen value, not a live
// computation" caveat WriteCsvFile's own comment already documents for
// CSV, not a new limitation this format introduces. Returns false (with
// `error` set) on a missing/unreadable file, a bad zip (missing/corrupt
// central directory, unsupported compression method, a CRC-32 or size
// mismatch), or a workbook with no readable worksheet/no rows.
bool ReadXlsxCells(const std::string& path, std::vector<std::vector<std::string>>& rows, std::string& error);

}  // namespace dino8::app::drafting
