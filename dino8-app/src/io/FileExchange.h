// Drafting / exchange formats that OpenNURBS does not cover: DXF (read and
// write), DWG (read and write, via GNU LibreDWG - see the DWG section
// below), SVG and PDF vector output of the current view (write), and ASCII
// PLY meshes (read and write). The DXF/SVG/PDF/PLY writers are hand-rolled -
// no external libraries - and every entity carries its layer so a drawing
// round-trips through AutoCAD, Illustrator, Inkscape or a PDF viewer with
// structure intact. DWG is the one exception to "no external libraries":
// unlike DXF it is not a published format, so ExportDwg/ImportDwg are built
// on GNU LibreDWG instead of hand-rolled parsing (see below).
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

// ---- DWG (via GNU LibreDWG) -------------------------------------------------
// Unlike every other format above, DWG is not a published spec Dino 8 can
// parse or emit on its own - see docs/INTEROP_LIMITATIONS.md for why a
// from-scratch DWG implementation is not a legally or practically viable
// "just write a parser" task. ExportDwg/ImportDwg instead link GNU
// LibreDWG (GPLv3, https://www.gnu.org/software/libredwg/), the same real
// open-source DWG library other free CAD tools (e.g. FreeCAD's importDWG
// addon) build on; see THIRD_PARTY_LICENSES.md for what that means for this
// codebase's own licensing.
//
// ExportDwg writes an AC1015 (AutoCAD 2000, the version LibreDWG's writer
// documents as reliable) DWG by handing ExportDxf's own DXF text to
// LibreDWG's DXF reader and then LibreDWG's DWG writer - so it carries
// exactly ExportDxf's entity coverage (LINE/CIRCLE/ARC/LWPOLYLINE/POLYLINE/
// 3DFACE, layers, colours) translated into real DWG bytes, not a hand-rolled
// subset. ImportDwg reads a DWG directly through LibreDWG's decoder and
// walks its model-space entities: LINE, POINT, CIRCLE, ARC, LWPOLYLINE
// (bulges + closed flag) and INSERT (block instances, flattened into
// transformed copies of the block's own entities - Dino 8 has no live GPU
// block reference, matching how Insert/InstantiateBlock already work for
// blocks defined in-app). TEXT, MTEXT, DIMENSION, HATCH, SPLINE, 3D
// solids/meshes and anything else are not read back (see `summary`'s
// skipped count) - the same honest gap ImportDxf already has for entities
// outside its own coverage.
bool ExportDwg(const Document& doc, const std::string& path, bool selected_only, std::string& error);
bool ImportDwg(Document& doc, const std::string& path, std::string& summary);

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
