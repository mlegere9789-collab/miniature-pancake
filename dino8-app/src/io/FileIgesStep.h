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

// ---- IFC (BIM) --------------------------------------------------------------
// A real, valid IFC4 file - the same ISO-10303-21 Part 21 physical-file
// syntax STEP above uses, just a different EXPRESS schema/entity
// vocabulary - built on the same P21 writer/reader as ExportStep/ImportStep.
// Deliberately scoped to geometry exchange, not authoring-tool BIM data:
// each exported object becomes one IFCBUILDINGELEMENTPROXY (a generic BIM
// element, since Dino 8 has no wall/door/beam classification to map onto
// IFC's real building-element types) inside a minimal but complete
// IFCPROJECT/IFCSITE/IFCBUILDING/IFCBUILDINGSTOREY spatial hierarchy real
// IFC consumers expect, with its shape as an IFCTRIANGULATEDFACESET (IFC4's
// own tessellated-mesh representation, the same "no fabricated precision"
// scope ExportPly/ExportMeshFile already apply to Breps/Surfaces/SubDs -
// they're tessellated to a mesh first, not carried through as NURBS/B-rep,
// unlike ExportStep/ExportIges above). Every exported GlobalId is a real,
// unique 22-character string from IFC's own base64-like GUID alphabet -
// see DigitalSignature-style honesty note on GenerateIfcGuid() in the .cpp
// for exactly how it differs from the official buildingSMART UUID
// compression. Verified against a real third-party IFC toolkit
// (IfcOpenShell), not just this codebase's own reader, during development.
bool ExportIfc(const Document& doc, const std::string& path, bool selected_only, std::string& error);

// Reads every IFCTRIANGULATEDFACESET in the file (resolving its
// IFCCARTESIANPOINTLIST3D coordinate list and CoordIndex triangle list) and
// merges them into a single mesh object - the same "whole file as one
// mesh, no per-object split" scope ImportMeshFile's own OBJ reader already
// documents. `summary` describes what was read.
bool ImportIfc(Document& doc, const std::string& path, std::string& summary);

// ---- STEP AP242 (tessellated geometry) -------------------------------------
// Wires the kernel's existing Mesh::SaveStepAp242/LoadStepAp242 (a real
// AP242 - ISO 10303-242 - Part 21 file using AP242's own COORDINATES_LIST/
// TRIANGULATED_FACE tessellated-geometry entities, not AP214's B-rep ones)
// into the app, the same "kernel support already existed, nothing at the
// app level ever called it" gap ExportXyz/ImportXyz (FileExchange.h) closed
// for point clouds. Every exportable object is tessellated exactly like
// ExportIfc above (TessellateForIfc) and merged into one mesh, since the
// kernel API is single-mesh - the same scope ExportPly already has.
bool ExportStepAp242(const Document& doc, const std::string& path, bool selected_only, std::string& error);

// Reads a STEP AP242 file written by ExportStepAp242 (or any other
// reasonably well-formed single-TRIANGULATED_FACE AP242 file) into one mesh
// object. ImportStep above calls this automatically when a .stp/.step
// file's own FILE_SCHEMA names AP242 rather than AP214/AP203, so Open/
// Import need no special syntax for either schema.
bool ImportStepAp242(Document& doc, const std::string& path, std::string& summary);

}  // namespace dino8::app
