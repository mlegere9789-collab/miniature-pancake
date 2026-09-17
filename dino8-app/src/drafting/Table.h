// Tables (grid lines + cell text glyph curves as one annotation group),
// shared by Table, RevisionTable, TitleBlock and BillOfMaterials.
//
// A table is stored the same way every other annotation is (see
// annotate_common.h): a group of curves. The grid lines carry
//   Annotation = "Table" (or "RevisionTable" / "TitleBlock")
//   TableData  = the cell contents and column widths as JSON user text,
//                so a later edit (TableSetCell / TableEdit panel) can
//                rebuild the whole group from that one tag.
#pragma once

#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/types.h"

namespace dino8::app::drafting {

struct TableSpec {
  int rows = 0, cols = 0;
  std::vector<std::string> cells;         // row-major, rows*cols entries
  std::vector<double> col_widths;         // model units, size() == cols (0 => uniform default)
  double row_height = 0;                  // 0 => 2x text height
  double text_height = 0;                 // 0 => caller default
  kernel::Point3d origin{0, 0, 0};        // top-left corner
  ON_Plane plane;                         // orientation (origin overridden by `origin`)
  std::string title;                      // optional header banner spanning the width

  const std::string& Cell(int r, int c) const;
  double ColWidth(int c) const;
};

// Serializes/parses a TableSpec's cell data (not the plane/origin, which
// come from the group's own geometry) as compact JSON, e.g.
//   {"rows":2,"cols":2,"widths":[20,20],"rowh":8,"texth":3,"cells":["a","b","c","d"]}
std::string TableDataJson(const TableSpec& spec);
bool ParseTableDataJson(const std::string& json, TableSpec& spec);

}  // namespace dino8::app::drafting
