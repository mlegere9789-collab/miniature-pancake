// IGES 5.3 and STEP (ISO 10303-21, AP203/AP214) exchange, hand-rolled like
// the DXF / SVG / PDF / PLY support in FileExchange.h - no external
// libraries. Both readers build genuine ON_Brep topology (faces, loops,
// trims, edges) for trimmed surfaces and fall back to the untrimmed
// surface, with a count in the summary, when a face cannot be validated.
#pragma once

#include <string>

#include "doc/Document.h"

namespace dino8::app {

// ---- IGES -----------------------------------------------------------------
// Writes a fixed-column IGES 5.3 file: points (116), lines (110), circular
// arcs (100, with a 124 transformation when not in the world XY plane),
// every other curve as a rational B-spline (126), surfaces as 128, brep
// faces as trimmed surfaces (144 over 142 / 102 / 126 in both parameter and
// model space), meshes as one bilinear 128 per polygon (small meshes only),
// colours as 314 entities, layers as levels (with 406 form 3 level names),
// multi-face breps kept together by a 402 group and units in the G section.
bool ExportIges(const Document& doc, const std::string& path, bool selected_only, std::string& error);

// Reads 100, 102, 104, 106, 110, 112, 116, 118, 120, 122, 124, 126, 128,
// 140, 141, 142, 143, 144, 308/408, 314, 402 and 406 (level names). On
// success `summary` describes what was read, like the DXF reader.
bool ImportIges(Document& doc, const std::string& path, std::string& summary);

// ---- STEP -----------------------------------------------------------------
// Writes an AP214 Part 21 file with the standard product structure:
// MANIFOLD_SOLID_BREP / SHELL_BASED_SURFACE_MODEL for breps and surfaces
// (ADVANCED_FACE over PLANE, CYLINDRICAL_SURFACE or B_SPLINE_SURFACE_WITH_
// KNOTS, edges as LINE / CIRCLE / B_SPLINE_CURVE_WITH_KNOTS), FACETED_BREP
// for meshes, a GEOMETRIC_CURVE_SET for free curves and points, colours as
// STYLED_ITEMs and layers as PRESENTATION_LAYER_ASSIGNMENTs.
bool ExportStep(const Document& doc, const std::string& path, bool selected_only, std::string& error);

// Reads the DATA section (multi-line records, quoted strings, complex
// entities) and builds geometry for the common AP203/AP214 curve, surface,
// face, shell and solid entities; colours, names, layers and the length
// unit are honoured. `summary` describes the result.
bool ImportStep(Document& doc, const std::string& path, std::string& summary);

}  // namespace dino8::app
