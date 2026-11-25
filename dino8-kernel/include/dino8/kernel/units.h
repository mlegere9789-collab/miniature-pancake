#pragma once

// Length unit systems and exact unit-to-unit scale factors, shared by the
// .3dm Model wrapper (file_io.h) and the IGES / STEP curve exchange
// (curve.h: SaveIges/LoadIges/SaveStep/LoadStep with a UnitSystem).

namespace dino8::kernel {

// The length unit a Model's / exchange file's coordinates are measured in,
// kept independent of ON::LengthUnitSystem for the same reason Color/
// LinetypeSegment are kept independent of their own OpenNURBS counterparts.
enum class UnitSystem {
  Millimeters,
  Centimeters,
  Meters,
  Kilometers,
  Microns,
  Inches,
  Feet,
  Yards,
  Miles,
  None,
};

// Exact metres per one unit of `u` (international inch = 0.0254 m by
// definition; foot = 12 in, yard = 3 ft, mile = 5280 ft). 0.0 for
// UnitSystem::None, which has no length.
inline double MetersPerUnit(UnitSystem u) {
  switch (u) {
    case UnitSystem::Millimeters: return 0.001;
    case UnitSystem::Centimeters: return 0.01;
    case UnitSystem::Meters: return 1.0;
    case UnitSystem::Kilometers: return 1000.0;
    case UnitSystem::Microns: return 1e-6;
    case UnitSystem::Inches: return 0.0254;
    case UnitSystem::Feet: return 0.3048;
    case UnitSystem::Yards: return 0.9144;
    case UnitSystem::Miles: return 1609.344;
    case UnitSystem::None: return 0.0;
  }
  return 0.0;
}

// The factor that converts a length measured in `from` into the same length
// measured in `to` (`from` metres-per-unit over `to` metres-per-unit). 0.0
// if either side is UnitSystem::None.
inline double UnitScaleFactor(UnitSystem from, UnitSystem to) {
  const double a = MetersPerUnit(from), b = MetersPerUnit(to);
  return (a > 0.0 && b > 0.0) ? a / b : 0.0;
}

}  // namespace dino8::kernel
