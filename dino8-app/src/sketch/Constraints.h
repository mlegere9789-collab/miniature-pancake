// Parametric 2D sketch constraints on curve objects in the active CPlane
// (Rhino has no such feature - see AUDIT.md row 19). A small
// Gauss-Newton/Levenberg-Marquardt solver drives line/polyline vertices and
// circle/arc centres+radii to satisfy the constraints below.
//
// Constraints are stored in the *document* itself, as one JSON blob under
// Document::UserText()["dino8.constraints"] (see Save/Load below) - the
// same "plain data that round-trips through the .3dm's own document user
// text" trick the rest of the app already uses (SetDocumentUserText),
// rather than adding a parallel undo-aware container to Document. They
// therefore survive Save/Open, and Undo/Redo for free (document user text
// is part of the snapshot Document::BeginChange captures).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "doc/Document.h"
#include "viewport/Viewport.h"

namespace dino8::app { class Application; }

namespace dino8::sketch {

using dino8::app::Document;
using dino8::app::ObjectId;
using dino8::kernel::Point3d;

enum class ConstraintType {
  Coincident, Horizontal, Vertical, Parallel, Perpendicular, Tangent,
  EqualLength, EqualRadius, Distance, Angle, Radius, Fixed, Midpoint, Symmetric,
};

const char* ConstraintTypeName(ConstraintType t);
bool ParseConstraintType(const std::string& s, ConstraintType& out);
// Every type name, in the order Constrain's Type= option cycles through them.
std::vector<std::string> ConstraintTypeNames();
// How many curve objects Constrain must collect (via WantObjects, one
// selection per call) before it can build a constraint of this type.
int RequiredObjectCount(ConstraintType t);
// Whether this type needs a following numeric value (Distance/Angle/Radius).
bool RequiresValue(ConstraintType t);

// A reference to one 2D point on a curve object: index >= 0 is a
// line/polyline control point (its vertex); index == -1 is a circle/arc's
// centre (there is no control point at the centre - it is reconstructed
// from ON_Arc::Center()/Radius() on read and write, see Constraints.cpp).
struct PointRef {
  ObjectId object = 0;
  int index = -1;
};

struct Constraint {
  int id = -1;
  ConstraintType type = ConstraintType::Coincident;
  // Point participants, meaning depends on `type` (see the table in
  // Constraints.cpp's BuildResiduals): most types reference 2 or 4 points
  // (one or two line segments, by their endpoints); Midpoint and
  // Symmetric reference 3 or 4.
  std::vector<PointRef> points;
  // Whole-object participants for Radius/EqualRadius/Tangent, which need
  // an object's radius as a solver variable, not just a point.
  std::vector<ObjectId> radius_objects;
  double value = 0;  // Distance (model units) / Angle (degrees) / Radius (model units)
  // Fixed only: the world-space position points[0] is pinned to, recorded
  // when the constraint was created (Rhino-style "pin it where it is now").
  Point3d fixed_target{0, 0, 0};
};

// Loads/saves the whole constraint list for `doc`. Load never throws: a
// missing or malformed blob is treated as "no constraints yet".
std::vector<Constraint> LoadConstraints(const Document& doc);
void SaveConstraints(Document& doc, const std::vector<Constraint>& list);

// Appends `c` (assigning its id) and persists the list; returns the id.
int AddConstraint(Document& doc, Constraint c);
// Removes constraint `id`; returns whether one was found and removed.
bool DeleteConstraint(Document& doc, int id);
// Removes every constraint referencing `object` (an object about to be deleted).
void DeleteConstraintsOn(Document& doc, ObjectId object);

// Solves every stored constraint in place against `cplane` (the plane the
// solver's 2D unknowns are expressed in - normally the active viewport's
// CPlane). Returns true if every constraint's residual is within
// tolerance after solving; `report` gets one line per constraint
// describing its post-solve measurement (used by ConstraintSolve and by
// the smoke test to check "angle 90"/"length 10").
bool SolveAll(Document& doc, const dino8::app::ConstructionPlane& cplane, std::vector<std::string>& report);

// Per-frame hook: if a constrained object's geometry changed since the
// last call (cheap bounding-box comparison, no per-object revision counter
// exists in Document - see AUDIT.md row 19's task note), re-solves
// silently. Called from Application::Frame() after every command has had
// a chance to run, so a Move/gumball drag on a constrained line snaps back
// to the constraint within the same or next frame.
void AutoResolveFrame(dino8::app::Application& app);

// Preview line segments (x,y,z pairs) drawing a small glyph for every
// stored constraint at its point(s) - perpendicular tick marks, parallel
// arrows, a dimension-style leader for Distance/Angle/Radius - for the
// ConstraintsShow command / a "show glyphs" toggle.
std::vector<float> BuildGlyphs(const Document& doc, const dino8::app::ConstructionPlane& cplane);

}  // namespace dino8::sketch
