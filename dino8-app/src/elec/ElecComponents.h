// Parametric electrical schematic symbols (Rhino has none of these; part of
// the same "vertical-market toolset" push as arch/ArchComponents.h's
// architectural/mechanical/MEP components - see that header's own doc
// comment for the pattern this file follows almost verbatim: a flat struct
// per component (fields unused by `type` ignored), Build()/Rebuild() that
// turn it into ordinary curve SceneObjects, and persistence through
// Document::UserText()["dino8.elec"] as one JSON array (its own key,
// separate from "dino8.arch", so the two component families round-trip
// independently through Save/Open/Undo).
//
// SCOPE, EXPLICITLY: these are simplified, commonly-recognized 2D schematic
// symbols built from ordinary curve geometry (lines, polylines, one circle)
// - the same "starter set, not a certified standard" honesty as
// MechSizeRow's own doc comment in ArchComponents.h. Specifically OUT OF
// SCOPE, matching that file's "declined feature" discipline:
//   - No claim of IEEE 315 / ANSI Y32.2 / IEC 60617 certified symbol shapes.
//     A resistor zigzag, a two-plate capacitor, a circle+X lamp etc. are
//     drawn the way they are commonly recognized, not traced from a
//     licensed standard.
//   - No real electrical simulation, load-flow, or NEC/IEC code-compliance
//     calculation of any kind (same spirit as MepDiameterFromFlow's own
//     "rule of thumb, not code-compliant" caveat).
//   - No netlist/connectivity graph and no design-rule check (DRC). A
//     WireRun is a drawn polyline with an optional associative endpoint (see
//     ElecComponent's ref0/ref1 below) - not a node in an electrical graph,
//     and nothing here checks whether a circuit is complete, shorted, or
//     correctly rated.
//   - PanelSchedule (cmd_elec.cpp) is a data table only - no breaker-sizing,
//     load-balancing, or other panel-schedule engineering calculation.
//   - No PDF/DWG electrical-standard title-block import.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "doc/Document.h"

namespace dino8::elec {

using dino8::app::Document;
using dino8::app::ObjectId;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

enum class ElecType {
  Resistor, Capacitor, Switch, Ground, Lamp, WireRun,
};

const char* ElecTypeName(ElecType t);
bool ParseElecType(const std::string& s, ElecType& out);
std::vector<std::string> ElecTypeNames();

// One parametric schematic symbol. Fields not used by `type` are ignored -
// same "flat struct rather than a variant" rationale as ArchComponent.
struct ElecComponent {
  int id = -1;
  ElecType type = ElecType::Resistor;

  // Every symbol type: placement point. Resistor/Capacitor/Switch/Ground/
  // Lamp: p1 gives the symbol's in-plane direction only (its own distance
  // from p0 is ignored - same convention as ArchComponent's Bolt/Nut/Washer
  // axis point). WireRun: p0/p1 are the two endpoints themselves.
  Point3d p0{0, 0, 0}, p1{1, 0, 0};
  // "Up" reference used to build the symbol's local (forward, right) frame
  // the same way ArchComponent::normal builds a Wall/Beam's (forward,
  // right, up) frame: forward = unit(p1-p0), right = normal x forward. World
  // Z by default (a schematic drawn flat in a horizontal or the active
  // construction plane, forward horizontal).
  Vector3d normal{0, 0, 1};

  // Resistor: overall symbol length (lead-to-lead, along `forward`) and
  // zigzag peak-to-peak height (along `right`) - both are exactly the
  // symbol's own bounding-box width/height (see BuildResistor's own comment
  // for why no extra scale factor is needed).
  double length = 0.6;
  double height = 0.3;   // Resistor: zigzag height (see above); Switch: blade rise above the terminal line.

  // Capacitor: plate gap (bbox width, the two plates straddle p0 by
  // +-gap/2 along `forward`) and plate length (bbox height, each plate
  // spans +-plate_length/2 along `right`).
  double gap = 0.1;
  double plate_length = 0.4;

  // Switch: terminal separation (p0 to the far terminal, along `forward`);
  // reuses `height` above for the open blade's rise. See BuildSwitch's own
  // comment for the blade-tip/contact-stub ratios that fix the open-gap
  // distance as a function of `length`.
  // (length/height fields above are shared with Resistor - both are a
  // "symbol length"/"symbol rise" pair, just with different geometry built
  // from them per type, the same field-reuse style as ArchComponent's
  // height/thickness/width.)

  // Ground/Earth: overall size the stepped-line ladder derives from (see
  // BuildGround's own comment for the fixed proportions - bbox height ends
  // up 0.7x this value). Lamp: circle diameter (bbox = size x size).
  double size = 0.3;

  // WireRun associativity (mirrors annotate_common.h's DimRefObj1/
  // DimRefEnd1 pattern used by DimLinear/CenterLine - the same "static bake
  // unless a real object anchors this endpoint" contract as ArchComponent's
  // own host-Wall rebuild-on-edit): when a WireRun's endpoint coincides with
  // a real Point object or curve start/end at creation time, has_refN/refN/
  // endN record it so ElecRebuild (cmd_elec.cpp) can re-resolve that
  // object's *current* position before rebuilding the wire - so moving the
  // anchored object and re-running ElecRebuild keeps the wire attached, the
  // same "explicit recompute, not an automatic hook on every edit" shape as
  // UpdateBillOfMaterials/UpdateDimensions elsewhere in this app.
  bool has_ref0 = false, has_ref1 = false;
  ObjectId ref0 = 0, ref1 = 0;
  std::string end0, end1;  // "point", "start", or "end" - see FindPointAnchor (annotate_common.h)

  // Geometry ElecRebuild owns: every curve object Build() last produced for
  // this component (cleared and rewritten on every Rebuild()).
  std::vector<ObjectId> objects;
};

std::vector<ElecComponent> LoadElec(const Document& doc);
void SaveElec(Document& doc, const std::vector<ElecComponent>& list);

// Appends `c` (assigning its id), builds its geometry, persists the list,
// and returns the assigned id.
int AddElecComponent(Document& doc, ElecComponent c);
// Removes `id`'s geometry objects and the component record. Returns whether
// one was found.
bool DeleteElecComponent(Document& doc, int id);
// Looks up a stored component by its id, or by one of its built object ids
// (for "pick the symbol in the viewport" style editing).
bool FindElecComponent(const Document& doc, int id, ElecComponent& out);
bool FindElecComponentByObject(const Document& doc, ObjectId object, ElecComponent& out);

// Rebuilds one component's geometry in place: deletes its previous
// object(s), builds fresh ones from `updated`'s current parameters, records
// the new object id(s), and persists the updated list. Returns the
// component's (unchanged) id, or -1 if not found.
int RebuildElecComponent(Document& doc, const ElecComponent& updated);

// Rebuilds every stored component from scratch (used by File3dm load, where
// object ids the JSON refers to may have been remapped - mirrors
// arch::RebuildAll).
void RebuildAll(Document& doc);

}  // namespace dino8::elec
