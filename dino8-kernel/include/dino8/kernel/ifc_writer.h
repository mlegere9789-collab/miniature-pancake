#pragma once

#include <string>
#include <vector>

namespace dino8::kernel {

// A minimal IFC4 (ISO 16739-1, ISO 10303-21 STEP text) writer for the building elements the app's parametric components model:
// one IfcProject with IfcSite, IfcBuilding and one or more IfcBuildingStorey, and in each storey walls, slabs, columns and beams
// as IfcExtrudedAreaSolid bodies over a rectangle profile, with IfcElementQuantity (length, area, volume) and IfcPropertySet
// data. Pure text output, no geometry kernel; units are SI (metre, square metre, cubic metre, radian).
//
// Scope (what is NOT written): no IfcOwnerHistory (optional in IFC4), no materials, no openings or doors/windows, no profiles other
// than a rectangle, no representation maps or styles. The file is a valid, self-consistent STEP file with referential integrity
// (every #id used is defined); it is not certified against any IFC viewer's import.

struct IfcVec3 {
  double x = 0, y = 0, z = 0;
};

enum class IfcElementKind { Wall, Slab, Column, Beam };

enum class IfcQuantityKind { Length, Area, Volume };
struct IfcQuantity {
  IfcQuantityKind kind = IfcQuantityKind::Length;
  std::string name;
  double value = 0;
};

// One IfcPropertySingleValue. `text` is written as IfcLabel; `is_real` writes `real` as IfcReal instead.
struct IfcProperty {
  std::string name;
  std::string text;
  bool is_real = false;
  double real = 0;
};
struct IfcPropertySetSpec {
  std::string name;  // e.g. "Pset_WallCommon"
  std::vector<IfcProperty> properties;
};

// A swept-solid element: the rectangle profile (x_dim by y_dim, centred on the element origin in the placement's x/y plane) is
// extruded `depth` along the placement's local z axis.
//   Wall:   x_dim = length, y_dim = thickness, depth = height; origin = centre of the wall base at its start-to-end midpoint.
//   Slab:   x_dim, y_dim = footprint, depth = thickness; origin = footprint centre on the top face (the slab extrudes downward:
//           set axis_z = (0, 0, -1)).
//   Column: x_dim, y_dim = section, depth = height; origin = centre of the base.
//   Beam:   x_dim = width, y_dim = height of the section, depth = length; the local z axis runs along the beam.
// axis_z is the extrusion axis, ref_x the local x direction (any vector not parallel to axis_z; it is orthogonalised).
struct IfcElementSpec {
  IfcElementKind kind = IfcElementKind::Wall;
  std::string name;
  IfcVec3 origin;  // world coordinates (metres)
  IfcVec3 axis_z{0, 0, 1};
  IfcVec3 ref_x{1, 0, 0};
  double x_dim = 1, y_dim = 1, depth = 1;
  // Quantities written in addition to the standard base quantities the writer derives from the geometry (see below).
  std::vector<IfcQuantity> extra_quantities;
  std::vector<IfcPropertySetSpec> property_sets;
};

struct IfcStoreySpec {
  std::string name;
  double elevation = 0;
  std::vector<IfcElementSpec> elements;
};

struct IfcModelSpec {
  std::string project_name = "Project";
  std::string site_name = "Site";
  std::string building_name = "Building";
  std::vector<IfcStoreySpec> storeys;
  std::string file_name = "model.ifc";
  std::string timestamp = "2026-01-01T00:00:00";  // ISO 8601, written to FILE_NAME verbatim (fixed so output is reproducible)
  std::string author = "Dino 8";
};

// The base quantities the writer derives from the extrusion geometry for each kind (all exact products of the dimensions):
//   Wall   (Qto_WallBaseQuantities):   Length = x_dim, Width = y_dim, Height = depth, GrossFootprintArea = x_dim * y_dim,
//                                      GrossSideArea = x_dim * depth, GrossVolume = x_dim * y_dim * depth
//   Slab   (Qto_SlabBaseQuantities):   Depth = depth, Perimeter = 2 * (x_dim + y_dim), GrossArea = x_dim * y_dim, GrossVolume
//   Column (Qto_ColumnBaseQuantities): Length = depth, CrossSectionArea = x_dim * y_dim, GrossVolume
//   Beam   (Qto_BeamBaseQuantities):   Length = depth, CrossSectionArea = x_dim * y_dim, GrossVolume
std::vector<IfcQuantity> IfcBaseQuantities(const IfcElementSpec& e);

// Writes the IFC4 STEP text. Entity ids are consecutive from #1 in a fixed order, GlobalIds are derived deterministically from the
// names and positions, so the same model always yields the same bytes.
std::string WriteIfc4(const IfcModelSpec& model);

// Formats a real in STEP syntax (always with a decimal point, "1.E-05" style exponents, shortest round-trip-safe 12 digits).
std::string IfcReal(double v);
// STEP string literal: quotes doubled, backslash doubled, non-ASCII characters written as \X2\....\X0\ (BMP only; others '?').
std::string IfcString(const std::string& utf8);
// 22-character IfcGloballyUniqueId (128 bits in the IFC base-64 alphabet) derived from a seed string.
std::string IfcGuid(const std::string& seed);

}  // namespace dino8::kernel
