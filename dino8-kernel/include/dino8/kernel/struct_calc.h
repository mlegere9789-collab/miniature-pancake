#pragma once

#include <string>
#include <vector>

namespace dino8::kernel {

// Structural take-off and section-property formulas that need no geometry kernel: closed-form cross-section properties of the
// common framing shapes, member weight and volume, reinforcement mass / lap / stagger / rounding / layout counts, and quantity
// grouping by level and material. Pure functions over plain numbers in one consistent unit system (metres, kilograms, kg/m^3 in the
// tests). NOT design-code checks: every factor, density and limit is an input.

// ---------------------------------------------------------------------------------------------------------------------------
// Cross-section properties. The section lies in the (x, y) plane; x is horizontal (width / flange direction), y is vertical (depth
// direction, y = 0 at the bottom edge). Ix is about the horizontal centroidal axis (strong axis of an upright beam), Iy about the
// vertical centroidal axis.
struct SectionProps {
  bool valid = false;
  double area = 0;
  double ybar = 0;        // centroid height above the bottom edge
  double depth = 0;       // total height of the section
  double width = 0;       // total width of the section
  double ix = 0, iy = 0;  // second moments of area about the centroidal axes
  double zx_top = 0, zx_bottom = 0;  // elastic section moduli Ix / (distance to the extreme fibre)
  double zx = 0;                     // min(zx_top, zx_bottom)
  double zy = 0;                     // Iy / (width / 2)
  double rx = 0, ry = 0;             // radii of gyration sqrt(I / A)
  double plastic_zx = 0;             // plastic modulus about x (shapes where it has a closed form, else 0)
  double j = 0;                      // St Venant torsion constant (solid circle and thin-walled shapes only, else 0)
};

SectionProps RectSection(double b, double h);                   // solid rectangle b wide, h deep
SectionProps CircleSection(double d);                           // solid circle of diameter d
SectionProps HollowCircleSection(double d_outer, double t);     // circular tube, wall thickness t
SectionProps BoxSection(double b, double h, double t);          // rectangular tube, uniform wall thickness t
// Doubly symmetric I: flanges bf x tf, web tw between them, overall depth h (no root fillets).
SectionProps ISection(double bf, double tf, double tw, double h);
// T section, flange on top: flange bf x tf, stem tw below it, overall depth h.
SectionProps TSection(double bf, double tf, double tw, double h);

// Weight (mass) of a prismatic member: area * length * density. A member whose section area or density is not known (<= 0)
// reports zero, as the vendor feature states.
double MemberMass(double section_area, double length, double density);

// Cut length of a framing member: the modelled length plus the start / end extensions minus the start / end cutbacks (a join
// trims the member), never negative; volume = section area * cut length.
struct FramingMember {
  double length = 0;
  double start_extension = 0, end_extension = 0;
  double start_cutback = 0, end_cutback = 0;
  double section_area = 0;
};
struct FramingQuantities {
  double cut_length = 0;
  double volume = 0;
};
FramingQuantities ComputeFraming(const FramingMember& m);

// ---------------------------------------------------------------------------------------------------------------------------
// Reinforcement.
// Mass per metre of a plain round bar: pi * d^2 / 4 * density (steel 7850 kg/m^3).
double RebarMassPerLength(double diameter, double density = 7850.0);
// Mass of a bar or a set: total length * mass per unit length (set: count bars of bar_length each).
double RebarMass(double total_length, double mass_per_length);
double RebarSetMass(int count, double bar_length, double mass_per_length);

// Lap length = bar diameter * lap multiplier of the splice type; a manual override (> 0) replaces it.
double LapLength(double bar_diameter, double lap_multiplier, double manual_override = 0);
// Stagger length = lap length * stagger multiplier of the splice type.
double StaggerLength(double lap_length, double stagger_multiplier);

enum class RoundMethod { None, Nearest, Up, Down };
// Rounds a calculated bar or segment length to a multiple of `increment`. increment <= 0 or None returns the value unchanged.
double RoundLength(double value, RoundMethod method, double increment);

enum class BarLayoutRule { Single, FixedNumber, MaximumSpacing, NumberWithSpacing, MinimumClearSpacing };
struct BarLayoutInput {
  BarLayoutRule rule = BarLayoutRule::Single;
  double array_length = 0;  // distance from the first to the last bar centre (not used by NumberWithSpacing and Single)
  int number = 0;           // FixedNumber, NumberWithSpacing
  double spacing = 0;       // MaximumSpacing (centre-to-centre limit), NumberWithSpacing (exact)
  double min_clear = 0;     // MinimumClearSpacing: smallest gap between bar surfaces
  double bar_diameter = 0;
};
struct BarLayout {
  bool ok = false;
  int count = 0;
  double spacing = 0;  // actual centre-to-centre spacing
  double span = 0;     // first to last bar centre = (count - 1) * spacing
};
BarLayout ComputeBarLayout(const BarLayoutInput& in);

// ---------------------------------------------------------------------------------------------------------------------------
// Quantity take-off by level and material.
struct TakeoffItem {
  std::string level, material;
  double volume = 0;
};
struct TakeoffRow {
  std::string level, material;
  int count = 0;
  double volume = 0;
};
struct TakeoffResult {
  std::vector<TakeoffRow> rows;                // sorted by (level, material), one row per distinct pair
  std::vector<TakeoffRow> level_totals;        // material empty, sorted by level
  std::vector<TakeoffRow> material_totals;     // level empty, sorted by material
  double total_volume = 0;
  int total_count = 0;
};
TakeoffResult TakeOffByLevelMaterial(const std::vector<TakeoffItem>& items);

// ---------------------------------------------------------------------------------------------------------------------------
// Surface live loads of zones: each zone classification maps to a load category with a unit value (load per area); the zone load
// is the zone area times that value. Zones whose classification is unmapped get no load and are counted as unmapped.
struct LoadCategory {
  std::string classification;
  std::string category;
  double unit_value = 0;  // e.g. kN/m^2
};
struct ZoneLoadInput {
  std::string classification;
  double area = 0;
};
struct ZoneLoad {
  std::string category;
  double unit_value = 0;
  double load = 0;  // area * unit_value
  bool mapped = false;
};
struct ZoneLoadsResult {
  std::vector<ZoneLoad> zones;
  double total_load = 0;
  int unmapped = 0;
};
ZoneLoadsResult GenerateZoneLiveLoads(const std::vector<ZoneLoadInput>& zones, const std::vector<LoadCategory>& mapping);

}  // namespace dino8::kernel
