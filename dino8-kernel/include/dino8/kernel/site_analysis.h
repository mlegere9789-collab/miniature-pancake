#pragma once

// Site and massing analysis: sun position and sun hours, ground and facade shadowing, sky-view and vertical-sky-component
// factors, wind and noise screening metrics, plot area / coverage / floor-area-ratio / setback checks, mass floors, bearing
// traverses and a small TIN toolbox. Everything here is plain C++ on doubles (no openNURBS), in the right-handed frame
// x = east, y = north, z = up; azimuths are degrees clockwise from north, elevations degrees above the horizon.
//
// Sun position: the "low accuracy" solar coordinates of Meeus, Astronomical Algorithms, 2nd ed., chapter 25 (equations 25.2 to
// 25.7, apparent longitude with the nutation/aberration term, obliquity 22.2 with the 0.00256 cos(Omega) term), sidereal time
// 12.4, horizontal coordinates 13.5 / 13.6 and the equation of time of chapter 28. Meeus states the longitude of the Sun to
// 0.01 degree for the years 1900 to 2100; the derived error budget of the horizontal position is in SunPositionErrorBudgetDeg.
// Atmospheric refraction is NOT applied (elevations are geometric); sunrise and sunset use the conventional centre-of-disc
// altitude of -0.833 degree (refraction 34' plus semidiameter 16').

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dino8::kernel::site {

struct Vec2 {
  double x = 0, y = 0;
};
struct Vec3 {
  double x = 0, y = 0, z = 0;
};
using Polygon = std::vector<Vec2>;

// ---------------------------------------------------------------- polygons (areas are exact shoelace sums)

// Signed area, positive for counter-clockwise order (shoelace formula).
double SignedArea(const Polygon& p);
double Perimeter(const Polygon& p);
Vec2 Centroid(const Polygon& p);  // area centroid of a simple polygon
bool PointInPolygon(const Polygon& p, double x, double y);  // even-odd; boundary points are not specified
// Area of a region: |outer| minus the |hole| areas (holes must lie inside the outer loop and be disjoint).
double RegionArea(const Polygon& outer, const std::vector<Polygon>& holes);
// Convex hull, counter-clockwise (Andrew's monotone chain), collinear points dropped.
Polygon ConvexHull(std::vector<Vec2> pts);
// Clip a convex or arbitrary subject polygon to the half plane a*x + b*y <= c (Sutherland-Hodgman).
Polygon ClipHalfPlane(const Polygon& p, double a, double b, double c);
// Inward offset of a CONVEX counter-clockwise polygon by d (each edge moved inward by d; empty when it vanishes).
Polygon InsetConvex(const Polygon& p, double d);
// Mitred band of the given width around an open polyline (the centreline is shifted by alignment: 0 centred, +1 the band lies
// wholly to the left of the direction of travel, -1 wholly to the right). The result is a simple polygon whose area is
// width * length of the polyline when every segment is longer than the width times tan(turn/2).
Polygon MitredBand(const std::vector<Vec2>& centreline, double width, int alignment = 0);

// ---------------------------------------------------------------- sun

// Julian day number of a Gregorian calendar date and a UT hour (Meeus chapter 7).
double JulianDay(int year, int month, int day, double hourUT);

struct SunCoordinates {
  double apparentLongitudeDeg = 0;   // lambda of Meeus 25
  double obliquityDeg = 0;           // true obliquity of the ecliptic
  double rightAscensionDeg = 0;      // alpha (25.6), 0..360
  double declinationDeg = 0;         // delta (25.7)
  double distanceAU = 0;             // R (25.5)
  double equationOfTimeMinutes = 0;  // chapter 28, apparent minus mean solar time
};
SunCoordinates SunCoordinatesAt(double julianDay);

struct SunPosition {
  double azimuthDeg = 0;    // clockwise from north, 0..360 (plus the north angle of the model)
  double elevationDeg = 0;  // geometric, no refraction
};
// latitude north positive, longitude EAST positive. northAngleDeg: the angle by which the model's +y axis is rotated clockwise
// from true north (SketchUp's "north angle"); it is subtracted from the azimuth so that a model rotated by n sees every azimuth
// move by exactly -n.
SunPosition SunPositionAt(double julianDay, double latitudeDeg, double longitudeDeg, double northAngleDeg = 0.0);
// Documented error bound of SunPositionAt in degrees for zenith and azimuth (see the .cpp for the budget).
double SunPositionErrorBudgetDeg();
// Unit vector from the ground towards the sun in the x = east, y = north, z = up frame.
Vec3 SunDirection(const SunPosition& s);

struct SunTimes {
  bool polarDay = false;    // the Sun stays above -0.833 degree for the whole day
  bool polarNight = false;  // the Sun stays below it
  double sunriseHourUT = 0, sunsetHourUT = 0, solarNoonHourUT = 0;  // hours since 0h UT of the date (may be <0 or >24)
  double dayLengthHours = 0;
};
SunTimes SunriseSunset(int year, int month, int day, double latitudeDeg, double longitudeDeg);

// Shadow length on flat ground of a vertical object of the given height: height / tan(elevation); infinity at or below horizon.
double ShadowLength(double height, double elevationDeg);
// Inverse: object height from its shadow length at a known sun elevation (height = length * tan(elevation)).
double HeightFromShadow(double shadowLength, double elevationDeg);

// ---------------------------------------------------------------- obstructions and shadows

// A vertical prism: footprint polygon extruded from zBase to zBase + height.
struct Prism {
  Polygon footprint;
  double zBase = 0, height = 0;
};
Prism BoxPrism(double x0, double y0, double x1, double y1, double zBase, double height);

// Ground shadow of a prism for the given sun (convex footprint): the convex hull of the footprint and the footprint translated
// by height / tan(elevation) away from the sun. Empty when the sun is not above the horizon.
Polygon ShadowOnGround(const Polygon& footprint, double height, double sunAzimuthDeg, double sunElevationDeg);

// True when the segment from p along the unit direction dir (to infinity) meets the prism.
bool RayHitsPrism(const Vec3& p, const Vec3& dir, const Prism& prism);

struct SunSample {
  double azimuthDeg = 0, elevationDeg = 0;
  double hours = 0;  // time represented by this sample
};
// Samples of the Sun over one date at the given interval (UT midpoints between interval edges, local solar day around the
// longitude), keeping only those above the horizon; their hours add up to the sampled daylight.
std::vector<SunSample> SunSamplesForDay(int year, int month, int day, double latitudeDeg, double longitudeDeg,
                                        double stepMinutes, double northAngleDeg = 0.0);

struct GridSpec {
  double originX = 0, originY = 0;  // lower-left corner of the first cell
  double spacing = 1.0;
  int nx = 0, ny = 0;
  double z = 0;  // height of the sample plane
};
struct SunHoursGrid {
  GridSpec grid;
  std::vector<double> hours;  // nx * ny, row major (y outer), value at cell centres
  std::vector<char> valid;    // 0 where the sample point lies inside a building footprint (cannot be evaluated)
};
// Direct sun hours at the centre of every cell: the summed hours of the samples whose ray to the Sun misses every prism.
// Points inside a prism footprint (below its roof) get valid = 0 and hours = 0.
SunHoursGrid GroundSunHours(const GridSpec& grid, const std::vector<Prism>& obstructions, const std::vector<SunSample>& sun);
// Sun hours on the cells of a vertical rectangular facade (base from p0 to p1, outward normal on the left of p0 -> p1 looking
// down, height h, nu cells along and nv cells up), counting a sample only when the Sun is in front of the wall.
std::vector<double> FacadeSunHours(const Vec2& p0, const Vec2& p1, double height, int nu, int nv,
                                   const std::vector<Prism>& obstructions, const std::vector<SunSample>& sun, bool outwardIsLeft = true);

// Share (0..1) of the valid cells whose value is >= threshold / > threshold.
double ShareAtLeast(const std::vector<double>& values, const std::vector<char>* valid, double threshold);
double ShareAbove(const std::vector<double>& values, const std::vector<char>* valid, double limit);
// Indices of the values above a limit (for example facades above a noise limit).
std::vector<std::size_t> IndicesAbove(const std::vector<double>& values, double limit);

// ---------------------------------------------------------------- sky

enum class SkyModel { Uniform, CieOvercast };
// Sky factor at point p on a surface with unit normal n: the illuminance (or, for the uniform sky, the irradiation weight)
// delivered by the part of the dome not hidden by the prisms, divided by the same quantity for an unobstructed HORIZONTAL
// plane. With a CIE overcast sky and a vertical normal this is the vertical sky component (VSC); with a uniform sky and an
// upward normal it is the sky-view factor. The dome is integrated on na x ne equal-angle cells (midpoint rule, exact solid
// angle weights).
double SkyFactor(const Vec3& p, const Vec3& n, const std::vector<Prism>& obstructions, SkyModel sky, int na = 720, int ne = 180);

// ---------------------------------------------------------------- wind and noise screening

// Neutral logarithmic wind profile: u(z) = uRef * ln(z / z0) / ln(zRef / z0).
double LogLawSpeed(double uRef, double zRef, double z, double z0);
// Free-field point source: Lp = Lw - 20 log10(r) - 11 (ISO 9613-2 equation 7: the geometric divergence term 20 lg r + 11).
double PointSourceLevel(double soundPowerLevelDb, double distance);

// ---------------------------------------------------------------- plots, buildings, programme

struct Building {
  Polygon footprint;
  int floors = 1;
  double floorHeight = 3.0;
  std::vector<std::string> floorFunction;  // per floor; shorter than floors means the remaining floors use "unassigned"
};
double BuildingHeight(const Building& b);
double GrossFloorArea(const Building& b);
std::map<std::string, double> GfaByFunction(const std::vector<Building>& buildings);

// Context height estimate for a footprint with optional tags: an explicit height wins, else levels * 3 m, else one level (3 m).
double EstimateContextHeight(std::optional<double> heightTag, std::optional<int> levelsTag, double metresPerLevel = 3.0);

// A line building: a mitred band of the given width around the centreline, extruded floors x floorHeight.
struct LineBuilding {
  std::vector<Vec2> centreline;
  double width = 12.0;
  int alignment = 0;
  int floors = 4;
  double floorHeight = 3.0;
};
struct LineBuildingMetrics {
  double footprintArea = 0, volume = 0, grossFloorArea = 0;
};
LineBuildingMetrics MeasureLineBuilding(const LineBuilding& lb);

struct EnvelopeRules {
  double setback = 0;           // minimum distance from every plot edge (convex plots)
  double maxHeight = 1e300;
  double maxCoverage = 1e300;   // footprint area / plot area
  double maxFar = 1e300;        // gross floor area / plot area
};
struct EnvelopeReport {
  double plotArea = 0, footprintArea = 0, grossFloorArea = 0, coverage = 0, far = 0;
  std::vector<std::size_t> setbackViolations, heightViolations;  // building indices
  bool coverageViolation = false, farViolation = false;
  bool Compliant() const { return setbackViolations.empty() && heightViolations.empty() && !coverageViolation && !farViolation; }
};
EnvelopeReport CheckEnvelope(const Polygon& convexPlotCcw, const std::vector<Building>& buildings, const EnvelopeRules& rules);

// Number of cells of side `cell` whose centres lie inside the disc (analysis-area clip).
std::size_t CountCellCentresInDisc(double cx, double cy, double radius, double cell, double gridMinX, double gridMinY, int nx, int ny);

// Parking: stalls of width sw x length sl packed in a W x L rectangle in double-loaded modules (two stall rows of length sl
// sharing one aisle of width aisle, stalls side by side along W); a single-loaded row is added when sl + aisle still fits.
int PackParkingStalls(double W, double L, double sw, double sl, double aisle);

// ---------------------------------------------------------------- mass floors

// A mass made by linearly lofting a base polygon to a similar copy scaled by topScale about the base centroid over height H.
struct TaperedMass {
  Polygon base;
  double height = 0, topScale = 1.0;
};
struct MassLevel {
  double elevation = 0;
  double floorArea = 0, floorPerimeter = 0;  // section of the mass at the level elevation
  double volumeAbove = 0;                    // volume of the slab from this level to the next (last level: to the top)
};
// Section at every level elevation (0 <= e <= height); slab volumes are integrated exactly (the section area is quadratic).
std::vector<MassLevel> MassFloors(const TaperedMass& m, const std::vector<double>& levelElevations);

// ---------------------------------------------------------------- bearings, survey point, terrain, TIN

struct BearingLeg {
  char from = 'N';   // 'N' or 'S'
  double degrees = 0;
  double minutes = 0, seconds = 0;
  char toward = 'E';  // 'E' or 'W'
  double distance = 0;
};
// Azimuth in degrees clockwise from north of a quadrant bearing, and the inverse.
double BearingToAzimuth(char from, double deg, double min, double sec, char toward);
// Text such as N 45°00'00" E 14.142 for the leg from a to b (distance with the given decimals).
std::string FormatBearingLabel(const Vec2& a, const Vec2& b, int distanceDecimals = 3);
std::vector<Vec2> TraverseFromBearings(const Vec2& start, const std::vector<BearingLeg>& legs);

// Project base point / survey point style transform: shared (E, N, elevation) of a local point, the local frame rotated
// counter-clockwise by angleToNorthDeg.
struct SurveyPoint {
  double easting = 0, northing = 0, elevation = 0, angleDeg = 0;
};
Vec3 LocalToShared(const SurveyPoint& sp, const Vec3& local);
Vec3 SharedToLocal(const SurveyPoint& sp, const Vec3& shared);

// Terrain from a regular DEM (row-major, y outer; cell = spacing): Horn's 3x3 slope and aspect at an interior node.
struct SlopeAspect {
  double slopeDeg = 0, aspectDeg = 0;  // aspect: compass direction the surface faces downhill, clockwise from north; -1 flat
};
SlopeAspect HornSlopeAspect(const std::vector<double>& z, int nx, int ny, double spacing, int ix, int iy);
// Cut and fill between two DEMs on the same grid, cells counted at their centres' mean of the four corner differences.
struct CutFill {
  double cut = 0, fill = 0;
};
CutFill CutFillVolume(const std::vector<double>& existing, const std::vector<double>& proposed, int nx, int ny, double spacing);

struct Tin {
  std::vector<Vec3> v;
  std::vector<std::array<int, 3>> tri;  // counter-clockwise seen from above
};
Tin MakeFlatTin(double width, double depth, double spacing, double z = 0);
double TinPlanArea(const Tin& t);
// A TIN is valid when every triangle is counter-clockwise with positive plan area, every edge is used by at most two
// triangles (once per direction) and the plan areas add up to `expectedArea` within tolerance.
bool TinIsValid(const Tin& t, double expectedArea, double tol = 1e-9);
std::size_t TinBoundaryEdgeCount(const Tin& t);
// Swap the diagonal shared by triangles a and b; false when they do not share an edge or the quad is not strictly convex.
bool FlipEdge(Tin& t, std::size_t a, std::size_t b);
// Insert a vertex at the centroid of triangle i (z interpolated): +1 vertex, +2 triangles.
int AddDetailAtFace(Tin& t, std::size_t i);
// Raise or lower the vertices within `radius` of (cx, cy) by offset * f(d / radius), f(0) = 1, f(1) = 0, f(t) = (1 + cos(pi t)) / 2.
void Smoove(Tin& t, double cx, double cy, double radius, double offset);
// Stamp a flat pad: vertices inside the rectangle get z = padZ, vertices within `margin` outside blend linearly from padZ to
// their own height by Chebyshev distance to the rectangle.
void StampPad(Tin& t, double x0, double y0, double x1, double y1, double padZ, double margin);
// Regular sloped mesh from three corner elevations of a parallelogram p1 p2 p3 p4 (in order): z4 = z1 + z3 - z2.
double FourthCornerElevation(double z1, double z2, double z3);

// Illuminance at a surface element from a point source of intensity I (candela) at distance d, incidence cosine c: I c / d^2.
double PointSourceIlluminance(double candela, double distance, double cosIncidence = 1.0);

}  // namespace dino8::kernel::site
