// Parametric architectural components (Rhino has none of these - see
// AUDIT.md's "declined feature" rows for Wall/Door/Window/Slab/Roof/Stair/
// Column/Beam): each is a small record of design parameters (endpoints,
// height, thickness...) that Build() turns into ordinary mesh SceneObjects
// in the document, the same way Constraints.h keeps its own model alongside
// plain curve objects.
//
// Persistence follows Constraints.h's approach exactly: the whole component
// list round-trips through Document::UserText()["dino8.arch"] as one JSON
// array, so it survives Save/Open and Undo/Redo for free (document user
// text is part of the snapshot Document::BeginChange captures) without a
// second undo-aware container living inside Document itself.
//
// A component owns the ObjectId(s) of the mesh(es) ArchEdit rebuilds when a
// parameter changes: Rebuild() removes the old object(s), builds fresh
// geometry from the current parameters, and records the new id(s) - so
// editing a Wall's height, say, never leaves a stale mesh in the document.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "doc/Document.h"

namespace dino8::arch {

using dino8::app::Document;
using dino8::app::ObjectId;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

// Bolt/Nut/Washer/IBeam/Channel/Angle (mechanical parts + structural
// shapes) and Duct/Pipe/Conduit (MEP runs) extend the same flat-struct/
// enum/Build()/Schedule pattern the original arch types established - see
// MechSizeTable()/MechSizeAt() below for the (deliberately small, starter)
// standard-size tables these draw from, and BuildGeometry()'s switch cases
// in the .cpp for how each turns its parameters into mesh geometry.
enum class ArchType {
  Wall, Door, Window, Slab, Roof, Stair, Column, Beam,
  Bolt, Nut, Washer, IBeam, Channel, Angle,
  Duct, Pipe, Conduit,
};

const char* ArchTypeName(ArchType t);
bool ParseArchType(const std::string& s, ArchType& out);
std::vector<std::string> ArchTypeNames();

// One row of a small, hand-entered "standard size" table (Bolt/Nut/Washer:
// ISO-metric-style approximate dimensions; IBeam/Channel/Angle: plausible
// structural-shape dimensions). THIS IS A STARTER SET FOR A FEW COMMON
// SIZES, NOT A REAL FASTENER- OR STRUCTURAL-SHAPE STANDARDS DATABASE (no
// substitute for ISO 4014/4032/7089 or AISC/Eurocode section tables) - see
// the .cpp for the exact values and where each field is used per type.
struct MechSizeRow {
  const char* name;
  double d0 = 0, d1 = 0, d2 = 0, d3 = 0;
};

// Returns the standard-size table for `t` (Bolt/Nut/Washer/IBeam/Channel/
// Angle only; empty for every other ArchType).
std::vector<MechSizeRow> MechSizeTable(ArchType t);
// Same table's `name` fields only, for a command's size-picker option list.
std::vector<std::string> MechSizeNames(ArchType t);
// Row `idx` of `t`'s table, clamped into range (empty table returns a
// zeroed row rather than indexing out of bounds).
MechSizeRow MechSizeAt(ArchType t, int idx);

// One parametric component. Fields not used by `type` are ignored (kept as
// a flat struct rather than a variant so LoadArch/SaveArch and the ImGui
// ArchEdit panel need no per-type visitor).
struct ArchComponent {
  int id = -1;
  ArchType type = ArchType::Wall;

  // Wall/Beam/Stair: base line (Stair: p0=base of run, p1=top-of-run
  // direction only, i.e. p1's height component is ignored - see BuildStair).
  Point3d p0{0, 0, 0}, p1{1, 0, 0};
  // Slab/Roof: footprint rectangle, p0..p1 diagonal, both projected onto
  // the plane through p0 with normal `normal` (world Z for a flat slab).
  Vector3d normal{0, 0, 1};

  double height = 2.4;      // Wall/Door/Window/Column: vertical extent (model units)
  double thickness = 0.2;   // Wall/Slab: perpendicular thickness; Beam/Column: depth
  double width = 0.9;       // Door/Window opening width; Column: width; Beam: width
  double sill_height = 0.9; // Window: height of the opening's bottom above the wall's base

  double ridge_height = 1.0;  // Roof: rise of the ridge above the eave plane
  int roof_style = 0;         // Roof: 0=gable, 1=shed (single slope)

  int step_count = 12;   // Stair: number of risers
  double rise = 0.18;    // Stair: riser height
  double run = 0.28;     // Stair: tread depth
  double stair_width = 1.0;

  // Bolt/Nut/Washer/IBeam/Channel/Angle: index into MechSizeTable(type).
  // Bolt/Nut/Washer: p0 = position, p1 = a point off p0 giving the bolt's
  // axis direction only (its own distance is ignored, same convention as
  // Stair's p1). IBeam/Channel/Angle: p0-p1 is the shape's own run line,
  // exactly like Beam.
  int size_index = 0;
  // Bolt: overall shank length (reuses `height`'s "vertical/axial extent"
  // meaning); Nut/Washer/IBeam/Channel/Angle ignore it (Nut/Washer's axial
  // extent is the table's own thickness; IBeam/Channel/Angle's run length
  // is |p1-p0|).

  // Duct/Pipe/Conduit: p0-p1 centreline, extruded exactly like Beam.
  double diameter = 0.15;   // Pipe/Conduit outer diameter; round Duct diameter
  int duct_round = 1;       // Duct only: 1 = round (uses `diameter`), 0 = rectangular (uses width x thickness)
  // A single, clearly-heuristic input for SizeDuct/SizePipe below - NOT a
  // code-compliance calculation (see MepSizeFromFlow()'s own doc comment).
  double flow_rate = 100.0; // Duct: airflow in CFM; Pipe: flow in GPM; unused by Build()/BuildGeometry() itself

  ObjectId host = 0;  // Door/Window: the Wall component id the opening is cut into (0 = none)

  // Geometry ArchEdit/Rebuild owns: every mesh object Build() last produced
  // for this component (cleared and rewritten on every Rebuild()).
  std::vector<ObjectId> objects;
};

std::vector<ArchComponent> LoadArch(const Document& doc);
void SaveArch(Document& doc, const std::vector<ArchComponent>& list);

// Appends `c` (assigning its id), builds its geometry, persists the list,
// and returns the assigned id.
int AddArchComponent(Document& doc, ArchComponent c);
// Removes `id`'s geometry objects and the component record. Also, for a
// Wall, removes any Door/Window hosted on it (their opening was cut into
// geometry this call is about to delete). Returns whether one was found.
bool DeleteArchComponent(Document& doc, int id);
// Looks up a stored component by its id, or by one of its built object ids
// (for "pick the wall in the viewport" style editing).
bool FindArchComponent(const Document& doc, int id, ArchComponent& out);
bool FindArchComponentByObject(const Document& doc, ObjectId object, ArchComponent& out);

// Rebuilds one component's geometry in place: deletes its previous
// object(s), builds fresh ones from its current parameters, records the
// new object id(s), and persists the updated list. Re-cuts any Door/Window
// hosted on a rebuilt Wall too, so moving a wall keeps its openings
// attached. Returns the component's (unchanged) id, or -1 if not found.
int RebuildArchComponent(Document& doc, const ArchComponent& updated);

// Rebuilds every stored component from scratch (used by File3dm load,
// where object ids the JSON refers to may have been remapped).
void RebuildAll(Document& doc);

// ---------------------------------------------------------------------------
// MEP sizing heuristic (SizeDuct/SizePipe commands, cmd_arch.cpp).
//
// THIS IS A SIMPLIFIED, ORDER-OF-MAGNITUDE SIZING RULE OF THUMB, NOT AN
// ASHRAE- OR NEC-CODE-COMPLIANT DESIGN CALCULATION. Real duct/pipe sizing
// depends on friction loss, fitting counts, noise criteria, and licensed
// standards text (ASHRAE Fundamentals/Duct Fitting Database, NEC Chapter 9
// conduit fill tables, etc.) this project has no license to reproduce and
// no engineering-liability basis to certify. This function exists only to
// turn a plausible flow input into a plausible starting cross-section for
// a parametric model, exactly the spirit of the audit's own "declined
// feature" callouts elsewhere in this file.
//
// Formula (round cross-section): given an assumed duct/pipe face velocity
// `velocity_m_s` (a fixed constant the caller supplies - a real design
// would vary it by duct branch/pipe service), continuity gives
// cross-section area A = flow / velocity, and diameter d = sqrt(4*A/pi)
// for a round section. `flow_m3_s` is volumetric flow already converted to
// m^3/s (Duct's ArchComponent::flow_rate is stored in CFM - the caller
// converts via the well-known 1 CFM = 0.00047194745 m^3/s before calling).
double MepDiameterFromFlow(double flow_m3_s, double velocity_m_s);

}  // namespace dino8::arch
