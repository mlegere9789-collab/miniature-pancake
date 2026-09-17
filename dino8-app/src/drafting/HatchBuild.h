// Plain-Document hatch builders shared by the Hatch command (cmd_drafting2.cpp)
// and DXF/DWG HATCH import (io/FileExchange.cpp), so an imported hatch is
// built by the exact same code path as one made in-app and is therefore
// indistinguishable from it: same object kind(s), same "Hatch" group and
// user_text tags (Hatch/HatchSpacing/HatchRotation/HatchBoundary), so
// SelHatch / HatchScale / the Hatch panel all see it as a real hatch.
//
// These take a Document& directly rather than a CommandContext&, so they
// are usable from importers that have no active command (see
// geom/TextOutline.h for the same "pure geometry/doc function" pattern used
// for TEXT import).
#pragma once

#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/curve.h"
#include "doc/Document.h"
#include "drafting/HatchLibrary.h"

namespace dino8::app {
// Declared (defined in commands/CommandEngine.cpp) rather than pulled in via
// commands/cmd_common.h: that header also declares its own inline
// CurveFromON(const ON_Curve&, kernel::NurbsCurve&), which collides
// (ambiguous overload) with io/FileExchange.cpp's own identically-signatured
// helper of the same name once both are visible unqualified in that
// translation unit - so this header stays decoupled from commands/ entirely.
std::string FormatNumber(double v);
}  // namespace dino8::app

namespace dino8::app::drafting {

// Samples a closed curve into a polygon (no repeated closing point). A
// standalone copy of commands/hatch_common.h's BoundaryPolygon, kept
// separate for the same reason FormatNumber is forward-declared above.
inline std::vector<kernel::Point3d> SampleClosedBoundary(const kernel::NurbsCurve& c) {
  std::vector<kernel::Point3d> poly;
  for (double t : c.SuggestedParameterValues(0.005)) poly.push_back(c.PointAt(t));
  if (poly.size() > 1 && poly.front().DistanceTo(poly.back()) < 1e-9) poly.pop_back();
  return poly;
}

// A closed, solid-filled boundary: builds one trimmed-planar-brep object,
// tagged Hatch=Solid. Same as LibraryHatchCommand's `solid` case.
inline bool BuildSolidHatch(Document& doc, const kernel::NurbsCurve& boundary, ObjectId boundary_id, int layer,
                             double tolerance, const Color* color = nullptr) {
  ON_Plane pl;
  if (!boundary.raw().IsPlanar(&pl, tolerance)) return false;
  ON_Brep* b = ON_BrepTrimmedPlane(pl, boundary.raw());
  if (!b) return false;
  kernel::Brep k;
  k.raw() = *b;
  delete b;
  SceneObject s = SceneObject::MakeBrep(k);
  s.name = "Hatch Solid";
  s.layer_index = layer;
  s.user_text["Hatch"] = "Solid";
  s.user_text["HatchBoundary"] = std::to_string(boundary_id);
  if (color) { s.color = *color; s.color_by_layer = false; }
  doc.CreateGroup({doc.Add(std::move(s))}, "Hatch");
  return true;
}

// A closed boundary hatched with a named library pattern (e.g. ANSI31):
// builds one group of line-curve objects clipped to the boundary, tagged
// with the pattern name/scale/rotation/boundary like the Hatch command's
// own pattern case. Returns false if the boundary isn't planar or the
// pattern produced no geometry (e.g. spacing too coarse for the loop).
inline bool BuildPatternHatch(Document& doc, const HatchPattern& pattern, const kernel::NurbsCurve& boundary,
                               ObjectId boundary_id, int layer, double tolerance, double scale, double rotation,
                               kernel::Point3d base, const Color* color = nullptr, bool* truncated = nullptr) {
  ON_Plane pl;
  if (!boundary.raw().IsPlanar(&pl, tolerance)) return false;
  const std::vector<Loop> loops = {SampleClosedBoundary(boundary)};
  std::vector<kernel::NurbsCurve> lines = HatchPatternCurves(pattern, loops, pl, scale, rotation, base, 200000, truncated);
  if (lines.empty()) return false;
  std::vector<ObjectId> objs;
  for (const kernel::NurbsCurve& c : lines) {
    SceneObject s = SceneObject::MakeCurve(c);
    s.layer_index = layer;
    s.user_text["Hatch"] = pattern.name;
    s.user_text["HatchSpacing"] = FormatNumber(scale);
    s.user_text["HatchRotation"] = FormatNumber(rotation);
    s.user_text["HatchBoundary"] = std::to_string(boundary_id);
    if (color) { s.color = *color; s.color_by_layer = false; }
    objs.push_back(doc.Add(std::move(s)));
  }
  doc.CreateGroup(objs, "Hatch");
  return true;
}

}  // namespace dino8::app::drafting
