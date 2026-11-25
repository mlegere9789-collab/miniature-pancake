#pragma once

#include <string>
#include <vector>

namespace dino8::kernel {

// Architectural design rules and quantity calculations that do not need any geometry kernel: pure functions over plain numbers
// and 2D polygons, so the app's parametric components (arch/ArchComponents.h) and any command or schedule can share one tested
// implementation. Lengths are in one consistent unit (metres in the tests); areas and volumes follow.
//
// NOT code-compliance engines: every limit is an input (StairRules, ramp limits, ...), the defaults are common starting
// values and not any national building code.

struct ArchPt2 {
  double x = 0, y = 0;
};

// ---------------------------------------------------------------------------------------------------------------------------
// Stairs: riser / tread / width limits and the Blondel step formula 2*riser + tread (the "comfort" rule, ~0.63 m).
struct StairRules {
  double max_riser = 0.190;
  double min_riser = 0.100;
  double min_tread = 0.250;
  double min_width = 1.000;
  double blondel_min = 0.600;
  double blondel_max = 0.660;
};

struct StairCheck {
  bool ok = true;
  bool riser_ok = true, tread_ok = true, width_ok = true, blondel_ok = true;
  double blondel = 0;                  // 2 * riser + tread
  std::vector<std::string> violations; // one short sentence per broken rule
};
StairCheck CheckStair(double riser, double tread, double width, const StairRules& rules);

// One way to build a stair of total rise `total_rise`: `risers` risers of height `riser`, `risers - 1` treads of depth `tread`
// (the top floor acts as the last tread), horizontal run `run = (risers - 1) * tread`.
struct StairOption {
  int risers = 0;
  double riser = 0, tread = 0, run = 0;
};

// All riser counts that keep the riser inside [min_riser, max_riser], each with the tread the Blondel formula gives at the
// centre of its range (blondel_min + blondel_max) / 2 - 2 * riser, never below min_tread (a tread raised to min_tread can leave
// the Blondel range: such an option is dropped). Sorted by |risers - reference_risers| when reference_risers > 0, else by riser
// height, tallest first (fewest risers); ties go to the smaller riser count. `preferred_tread` > 0 replaces the Blondel tread
// when that tread satisfies min_tread and the Blondel range with the option's riser.
std::vector<StairOption> ProposeStairOptions(double total_rise, const StairRules& rules, double preferred_tread = 0,
                                             int reference_risers = 0);

// Story-linked stair: the riser count and tread are kept and the riser becomes new_total_rise / risers, as when a story height
// changes. valid = the new riser, the given tread and the Blondel rule all pass; otherwise `reasons` says why and `fixes` lists
// ProposeStairOptions for the new rise (the stair is flagged, not silently changed).
struct StairRelink {
  bool valid = false;
  double riser = 0;
  double blondel = 0;
  std::vector<std::string> reasons;
  std::vector<StairOption> fixes;
};
StairRelink RelinkStairToStory(int risers, double tread, double new_total_rise, const StairRules& rules);

// ---------------------------------------------------------------------------------------------------------------------------
// Ramps: steepest allowed slope (rise / horizontal run) and longest run before a landing.
struct RampPlan {
  bool ok = false;
  int flights = 0;
  int landings = 0;
  double run_per_flight = 0;   // horizontal
  double total_run = 0;        // flights * run_per_flight
  double total_length = 0;     // total_run + landings * landing_length (horizontal footprint)
  double slope = 0;            // total_rise / total_run
};
// Shortest ramp that climbs total_rise at no more than max_slope, split into the fewest equal flights of at most
// max_flight_run each, with a landing of landing_length between consecutive flights.
RampPlan PlanRamp(double total_rise, double max_slope, double max_flight_run, double landing_length);
// Slope and violations of a given ramp: `flights` equal flights each climbing rise_per_flight over run_per_flight.
struct RampCheck {
  bool ok = true;
  double slope = 0;
  std::vector<std::string> violations;
};
RampCheck CheckRamp(double rise_per_flight, double run_per_flight, int flights, double max_slope, double max_flight_run);

// ---------------------------------------------------------------------------------------------------------------------------
// Roof pitch in the three notations an architect types: degrees, percent (100 * rise / run) and rise per run. Negative pitches
// (a plane sloping down) are allowed; a vertical plane (|degrees| >= 90) is not a pitch.
struct RoofPitch {
  bool valid = false;
  double degrees = 0;
  double percent = 0;       // 100 * tan(degrees)
  double rise_per_run = 0;  // tan(degrees): rise for a run of 1
};
RoofPitch PitchFromDegrees(double degrees);
RoofPitch PitchFromPercent(double percent);
RoofPitch PitchFromRiseRun(double rise, double run);  // run must be non-zero
// Pitch of the plane through the point at height z_ref (horizontal distance 0 from the axis) and the clicked point at
// z_point, horizontal distance `distance` (> 0) from the axis.
RoofPitch PitchFromElevation(double z_ref, double z_point, double distance);

// ---------------------------------------------------------------------------------------------------------------------------
// Polygons and zones.
double PolygonAreaSigned(const std::vector<ArchPt2>& poly);  // shoelace; counter-clockwise positive
double PolygonArea(const std::vector<ArchPt2>& poly);        // |shoelace|

// How an element inside a zone affects it (Archicad's per-element zone relation): Bounds does nothing here (the boundary is the
// polygon itself), None is ignored, ReduceArea lowers the area only, Subtract lowers area and volume.
enum class ZoneEffect { None, ReduceArea, Subtract };
struct ZoneElement {
  double area = 0;  // plan area the element covers inside the zone
  ZoneEffect effect = ZoneEffect::Subtract;
};
struct ZoneQuantities {
  double gross_area = 0;   // shoelace of the zone polygon
  double net_area = 0;     // gross - elements (ReduceArea and Subtract) whose area >= min_element_area
  double volume = 0;       // (gross - Subtract elements with area >= min_element_area) * height
  int elements_counted = 0;
};
ZoneQuantities ComputeZone(const std::vector<ArchPt2>& poly, double height, const std::vector<ZoneElement>& elements,
                           double min_element_area);

// Door and window recesses: a recess adds its plan area to the zone when it is deep enough and/or large enough.
struct WallRecess {
  double depth = 0, area = 0;
};
enum class RecessRule { DepthAndArea, DepthOrArea };
double RecessAddedArea(const std::vector<WallRecess>& recesses, double min_depth, double min_area, RecessRule rule);

// Zone area reduced under low ceilings. The ceiling is the plane h(x, y) = h0 + gx * x + gy * y. Area where h < limit1 counts
// with (1 - pct1) of its value, and where h < limit2 (limit2 <= limit1, optional) with (1 - pct2) of its value. Percentages are
// fractions in [0, 1].
struct LowCeilingRule {
  double limit1 = 0, pct1 = 0;
  bool two_limits = false;
  double limit2 = 0, pct2 = 0;
};
struct LowCeilingResult {
  double full_area = 0;
  double area_below1 = 0;  // plan area with h < limit1
  double area_below2 = 0;  // plan area with h < limit2 (0 when only one limit)
  double counted_area = 0;
};
LowCeilingResult ZoneAreaUnderLowCeilings(const std::vector<ArchPt2>& poly, double h0, double gx, double gy,
                                          const LowCeilingRule& rule);
// The part of `poly` on the side a*x + b*y + c >= 0 (Sutherland-Hodgman; exact for the area of a simple polygon).
std::vector<ArchPt2> ClipPolygonHalfPlane(const std::vector<ArchPt2>& poly, double a, double b, double c);

// Room / area boundary at a chosen location of the bounding walls. `offsets[i]` is the distance, from the location line of the
// wall along polygon edge i (edge i runs vertex i -> i + 1) towards the room interior, at which the room boundary lies (negative
// = away from the room). The result polygon joins consecutive offset lines at their intersection (a mitre). The polygon may be
// clockwise or counter-clockwise. Edges that are parallel or consecutive collinear edges keep a shifted vertex on their line.
std::vector<ArchPt2> OffsetPolygonEdges(const std::vector<ArchPt2>& poly, const std::vector<double>& offsets);

enum class RoomBoundaryLocation { WallFinish, WallCenter, CoreCenter, CoreBoundary };
// A wall layered across its thickness, measured from the room side to the far side. thickness = finish_room + core +
// finish_far; the location line runs at `location_from_room` (distance from the room-side face).
struct LayeredWall {
  double finish_room = 0, core = 0, finish_far = 0;
  double location_from_room = 0;
};
// Offset of the chosen boundary from the location line towards the room interior (see OffsetPolygonEdges).
double RoomBoundaryOffset(const LayeredWall& wall, RoomBoundaryLocation where);
struct RoomQuantities {
  double area = 0, volume = 0;
};
RoomQuantities ComputeRoom(const std::vector<ArchPt2>& location_polygon, const std::vector<LayeredWall>& walls,
                           RoomBoundaryLocation where, double height);

// ---------------------------------------------------------------------------------------------------------------------------
// Wall quantities with openings. The wall runs x = 0 .. length along its location line, its top edge rises linearly from
// height_start to height_end (equal = a plain wall), thickness is constant. Openings are rectangles (x0, sill, width, height)
// in the wall's elevation; the part of an opening outside the wall (beyond the ends, below the base or above the top) is not
// subtracted.
struct WallOpening {
  double x0 = 0, width = 0, sill = 0, height = 0;
};
struct WallQuantitiesResult {
  bool ok = true;  // false when two openings overlap in elevation (their union would be double-counted)
  double gross_side_area = 0;   // one face, no openings
  double net_side_area = 0;
  double openings_area = 0;     // clipped to the wall
  double gross_volume = 0;
  double net_volume = 0;
  int openings_counted = 0;     // openings with a non-zero clipped area
};
WallQuantitiesResult ComputeWallQuantities(double length, double height_start, double height_end, double thickness,
                                           const std::vector<WallOpening>& openings);

// ---------------------------------------------------------------------------------------------------------------------------
// Composite wall skins: each skin becomes one component row (building material, thickness, net and gross volume). The skins run
// across the wall's thickness; every skin spans the same elevation face, so skin volume = skin thickness * face area (net face
// area has the openings removed, gross does not). The sum of the skin volumes equals the wall volume of ComputeWallQuantities
// when the skin thicknesses add up to the wall thickness.
struct WallSkin {
  std::string material;
  double thickness = 0;
};
struct WallSkinRow {
  std::string material;
  double thickness = 0;
  double gross_volume = 0;
  double net_volume = 0;
};
struct WallSkinQuantities {
  bool ok = true;  // false when the openings overlap (see ComputeWallQuantities)
  double total_thickness = 0;
  std::vector<WallSkinRow> rows;
  double gross_volume = 0;  // sum of the rows
  double net_volume = 0;
};
WallSkinQuantities ComputeWallSkinQuantities(double length, double height_start, double height_end, const std::vector<WallSkin>& skins,
                                             const std::vector<WallOpening>& openings);

// ---------------------------------------------------------------------------------------------------------------------------
// Thermal data of the layers of a composite skin: conductivity lambda (W/mK), density (kg/m^3), specific heat capacity (J/kgK).
// U-value = 1 / (R_si + sum(thickness / lambda) + R_se) in W/m^2K with the inner and outer surface resistances as inputs (the
// defaults are the common horizontal heat-flow values 0.13 and 0.04 m^2K/W). A layer with a non-positive conductivity or a
// negative thickness makes the result invalid.
struct ThermalLayer {
  double thickness = 0;
  double conductivity = 0;
  double density = 0;
  double heat_capacity = 0;
};
struct ThermalResult {
  bool valid = false;
  double r_layers = 0;       // sum(thickness / lambda)
  double r_total = 0;        // r_si + r_layers + r_se
  double u_value = 0;        // 1 / r_total
  double areal_heat_capacity = 0;  // sum(density * heat_capacity * thickness), J/m^2K
  double mass_per_area = 0;        // sum(density * thickness), kg/m^2
};
ThermalResult ComputeThermal(const std::vector<ThermalLayer>& layers, double r_si = 0.13, double r_se = 0.04);

}  // namespace dino8::kernel
