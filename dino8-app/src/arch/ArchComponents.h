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

enum class ArchType { Wall, Door, Window, Slab, Roof, Stair, Column, Beam };

const char* ArchTypeName(ArchType t);
bool ParseArchType(const std::string& s, ArchType& out);
std::vector<std::string> ArchTypeNames();

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

}  // namespace dino8::arch
