// Drafting / exchange formats that OpenNURBS does not cover: DXF (read and
// write), SVG and PDF vector output of the current view (write), and ASCII
// PLY meshes (read and write). All writers are hand-rolled - no external
// libraries - and every entity carries its layer so a drawing round-trips
// through AutoCAD, Illustrator, Inkscape or a PDF viewer with structure
// intact.
#pragma once

#include <string>

#include "doc/Document.h"

namespace dino8::app {

class Viewport;

// ---- DXF ------------------------------------------------------------------
// Writes an ASCII AC1015 (AutoCAD 2000) DXF with a LAYER table. Curves become
// LINE / CIRCLE / ARC / LWPOLYLINE (3D polylines as POLYLINE+VERTEX), points
// POINT, meshes 3DFACE, and breps / surfaces / SubDs their edge and isocurve
// polylines. `selected_only` restricts the export to the selection.
bool ExportDxf(const Document& doc, const std::string& path, bool selected_only, std::string& error);

// Reads LINE, LWPOLYLINE (closed flag + bulges), POLYLINE/VERTEX (including
// polyface meshes), CIRCLE, ARC, ELLIPSE, SPLINE, POINT, 3DFACE and the LAYER
// table. Other entities are skipped. On success `summary` describes what was
// read ("12 curves, 3 points, 1 mesh; 2 entities skipped").
bool ImportDxf(Document& doc, const std::string& path, std::string& summary);

// ---- Vector output (SVG / PDF) --------------------------------------------
struct DrawingOptions {
  // Page size in millimetres (default A4 landscape).
  double page_width_mm = 297.0;
  double page_height_mm = 210.0;
  double margin_mm = 10.0;
  // 0 = fit the geometry to the page; otherwise page millimetres per
  // document unit (1 with a millimetre document is 1:1). Only meaningful
  // for parallel projections - a perspective view is always fitted.
  double scale = 0.0;
  double line_width_mm = 0.25;
};

// Projects the visible objects (or just the selection) through `view`'s
// camera - or straight down the world Z axis (Top) when `view` is null -
// and writes them as stroked paths. Closed polylines end with a proper
// `Z` / `h` close so fills and joins work downstream.
bool ExportSvg(const Document& doc, const Viewport* view, const std::string& path, bool selected_only,
               const DrawingOptions& opts, std::string& error);
bool ExportPdf(const Document& doc, const Viewport* view, const std::string& path, bool selected_only,
               const DrawingOptions& opts, std::string& error);

// ---- PLY ------------------------------------------------------------------
bool ExportPly(const Document& doc, const std::string& path, bool selected_only, std::string& error);
bool ImportPly(Document& doc, const std::string& path, std::string& error);

}  // namespace dino8::app
