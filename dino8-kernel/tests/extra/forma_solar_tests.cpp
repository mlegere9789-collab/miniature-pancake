// Tests of include/dino8/kernel/forma_solar.h: sun position, sun hours on ground grids and on facade and roof cells, dates and time
// windows, shadows at a time, threshold shares, vertical sky component, sky view factor and guidance levels (Register rows
// D8-FORMA-ANALYSIS-0001 .. 0008).
//
// Every oracle is written here and does not call the code under test:
//  * Almanac(): the low-precision solar coordinates of the Astronomical Almanac (mean longitude and anomaly, ecliptic longitude,
//    right ascension, Greenwich mean sidereal time), a different formulation from the NOAA equations in the header.
//  * the published NREL SPA sample (2003-10-17 12:30:30 UTC-7, 39.742476 N, 105.1786 W: zenith 50.1116, azimuth 194.340 degrees; the
//    values are quoted in the rows' acceptance corpus).
//  * ShadowedByBox(): the closed-form shadow of a box, length h / tan(elevation), as a swept footprint intersected with a slab test in 2D.
//  * the sunrise equation cos(H0) = -tan(lat) tan(decl) for the length of the day.
//  * closed forms for the sky: vertical sky component on a facade (CIE overcast sky: 39.6177 % unobstructed, a numerical Simpson
//    integral over azimuth with the elevation integral done analytically for a parallel obstruction) and the sky view factor in an
//    infinite street canyon, 1 / sqrt(1 + (2 H / W)^2).
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/forma_solar.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::forma;

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// ---- independent oracles ---------------------------------------------------------------------------------------------------
struct AlmSun {
  double azDeg, elDeg, declDeg;
};
// Astronomical Almanac approximate solar coordinates (1950-2050, about 0.01 degree). UT as hours of the UTC day.
AlmSun Almanac(double latDeg, double lonDeg, int y, int m, int d, double utHours) {
  const double jd = 367.0 * y - std::floor(7.0 * (y + std::floor((m + 9) / 12.0)) / 4.0) + std::floor(275.0 * m / 9.0) + d + 1721013.5 +
                    utHours / 24.0;
  const double n = jd - 2451545.0;
  const double rad = kPi / 180.0;
  const double L = 280.460 + 0.9856474 * n;
  const double g = (357.528 + 0.9856003 * n) * rad;
  const double lam = (L + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g)) * rad;
  const double eps = (23.439 - 0.0000004 * n) * rad;
  const double ra = std::atan2(std::cos(eps) * std::sin(lam), std::cos(lam));
  const double dec = std::asin(std::sin(eps) * std::sin(lam));
  const double gmstHours = 18.697374558 + 24.06570982441908 * n;
  double H = std::fmod(gmstHours * 15.0 + lonDeg, 360.0) * rad - ra;   // local hour angle, west positive
  H = std::atan2(std::sin(H), std::cos(H));
  const double lat = latDeg * rad;
  const double sinEl = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(H);
  const double el = std::asin(sinEl);
  double az = std::atan2(-std::cos(dec) * std::sin(H), std::sin(dec) * std::cos(lat) - std::cos(dec) * std::sin(lat) * std::cos(H));
  double azDeg = az / rad;
  if (azDeg < 0) azDeg += 360.0;
  return {azDeg, el / rad, dec / rad};
}
double AngDiff(double a, double b) {
  double d = std::fmod(a - b + 540.0, 360.0) - 180.0;
  return std::fabs(d);
}

// Shadow of the box [x0,x1] x [y0,y1] x [0,h] on the ground for the sun at (azDeg, elDeg): the point q is shadowed when it lies in the
// footprint swept towards the sun by L = h / tan(el). `grow` expands the footprint (robustness check).
bool ShadowedByBox(double qx, double qy, double x0, double x1, double y0, double y1, double h, double azDeg, double elDeg, double grow = 0) {
  const double L = h / std::tan(elDeg * kPi / 180.0);
  const double ux = std::sin(azDeg * kPi / 180.0), uy = std::cos(azDeg * kPi / 180.0);   // horizontal unit vector towards the sun
  double lo = 0, hi = L;
  auto clip = [&](double q, double u, double a, double b) {
    a -= grow;
    b += grow;
    if (std::fabs(u) < 1e-15) return q >= a && q <= b;
    double t1 = (a - q) / u, t2 = (b - q) / u;
    if (t1 > t2) std::swap(t1, t2);
    lo = std::max(lo, t1);
    hi = std::min(hi, t2);
    return true;
  };
  if (!clip(qx, ux, x0, x1)) return false;
  if (!clip(qy, uy, y0, y1)) return false;
  return lo <= hi;
}

std::vector<SunSample> AuthoredSamples(const std::vector<std::pair<double, double>>& azEl) {
  std::vector<SunSample> v;
  for (auto& p : azEl) {
    SunSample s;
    s.dir = DirFromAzEl(p.first, p.second);
    s.elevationDeg = p.second;
    v.push_back(s);
  }
  return v;
}

// ---- ANALYSIS-0001 ---------------------------------------------------------------------------------------------------------
// The NOAA position agrees with two independent references, and sun hours on a ground grid behind a box follow the closed-form shadow.
//
// Position: the SPA sample (published zenith 50.1116, azimuth 194.340; that value includes atmospheric refraction, about 0.016 degree
// at 40 degrees elevation, and NOAA states about 0.01 degree for its equations, so the tolerance is 0.03 degree) and twelve instants
// against the Almanac formulation (each method about 0.01 degree, tolerance 0.03 degree on elevation, 0.05 on azimuth).
// Ground grid: a box [0,10] x [0,10] x [0,20] on flat ground, 13 authored sun directions (azimuth 60 to 300 step 20, elevation
// alternating 25 and 55 degrees: shadow lengths 42.89 and 14.00 m), 19 x 19 grid points 5 m apart from (-39.7, -41.3). A point has
// as many lit samples as directions whose swept footprint (length h / tan(el)) misses it.
D8_EXTRA_TEST(Rw5FormaSunHoursGroundGrid) {
  const SunPos spa = SunPosition(39.742476, -105.1786, 2003, 10, 17, 12.0 + 30.0 / 60 + 30.0 / 3600 + 7.0);
  d8test::Check(Near(90.0 - spa.elevationDeg, 50.1116, 0.03) && Near(spa.azimuthDeg, 194.340, 0.03),
                "sun position: the NREL SPA sample (zenith 50.1116, azimuth 194.340) within 0.03 degree");
  d8test::Check(!Near(90.0 - spa.elevationDeg, 50.1116, 0.001), "sun position: the geometric NOAA zenith is not the refracted SPA value to the digit (a copied constant would be)");
  struct Inst { double lat, lon; int y, m, d; double h; };
  const Inst insts[] = {{51.5, -0.1, 2026, 6, 21, 7.0},   {-33.9, 151.2, 2026, 12, 21, 2.0}, {40.0, -105.0, 2026, 3, 20, 18.5},
                        {10.0, 20.0, 2025, 9, 22, 12.0},  {60.0, 10.0, 2026, 12, 21, 10.0},  {35.0, 139.0, 2027, 7, 4, 3.5},
                        {-45.0, -70.0, 2026, 1, 15, 15.0}, {48.0, 2.3, 2024, 2, 29, 8.25},   {0.0, 0.0, 2026, 3, 20, 9.0},
                        {25.0, 55.0, 2030, 10, 10, 10.0}, {70.0, 25.0, 2026, 5, 1, 21.0},   {33.0, -118.0, 2026, 8, 8, 21.0}};
  double worstEl = 0, worstAzRatio = 0;
  for (const Inst& i : insts) {
    const SunPos a = SunPosition(i.lat, i.lon, i.y, i.m, i.d, i.h);
    const AlmSun b = Almanac(i.lat, i.lon, i.y, i.m, i.d, i.h);
    worstEl = std::max(worstEl, std::fabs(a.elevationDeg - b.elDeg));
    // an angular error d on the sphere is d / cos(el) in azimuth: compare the azimuth error scaled by cos(el)
    if (std::fabs(b.elDeg) < 85) worstAzRatio = std::max(worstAzRatio, AngDiff(a.azimuthDeg, b.azDeg) * std::cos(b.elDeg * kPi / 180.0));
  }
  d8test::Check(worstEl < 0.02, "sun position: elevation agrees with the Almanac formulation at twelve instants (worst below 0.02 degree)");
  d8test::Check(worstAzRatio < 0.02, "sun position: azimuth error times cos(elevation) agrees with the Almanac formulation at twelve instants (below 0.02 degree)");
  d8test::Check(worstEl > 1e-5, "sun position: the two formulations are different methods (not the same numbers)");

  const std::vector<Tri> box = PrismTriangles({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 0, 20);
  std::vector<std::pair<double, double>> azEl;
  for (int i = 0; i < 13; ++i) azEl.push_back({60.0 + 20.0 * i, i % 2 ? 55.0 : 25.0});
  const std::vector<SunSample> samples = AuthoredSamples(azEl);
  int mismatches = 0, shadedPoints = 0, ambiguous = 0;
  long litKernel = 0, litOracle = 0, litWrong = 0;
  for (int jy = 0; jy < 19; ++jy)
    for (int jx = 0; jx < 19; ++jx) {
      const double x = -39.7 + 5.0 * jx, y = -41.3 + 5.0 * jy;
      int oracle = 0, wrong = 0, oracleGrow = 0, oracleShrink = 0;
      for (auto& p : azEl) {
        if (!ShadowedByBox(x, y, 0, 10, 0, 10, 20, p.first, p.second)) ++oracle;
        if (!ShadowedByBox(x, y, 0, 10, 0, 10, 20, p.first, p.second, 1e-4)) ++oracleGrow;
        if (!ShadowedByBox(x, y, 0, 10, 0, 10, 20, p.first, p.second, -1e-4)) ++oracleShrink;
        // decoy: a shadow length of h * tan(el) instead of h / tan(el), i.e. a box of height h' = h tan^2(el)
        const double t = std::tan(p.second * kPi / 180.0);
        if (!ShadowedByBox(x, y, 0, 10, 0, 10, 20.0 * t * t, p.first, p.second)) ++wrong;
      }
      if (oracleGrow != oracleShrink) ++ambiguous;
      const SunHoursResult r = SunHoursAt({x, y, 0}, {0, 0, 0}, box, samples, 1, 1);
      const int kernel = static_cast<int>(std::lround(r.totalHours));
      if (kernel != oracle) ++mismatches;
      if (oracle < 13) ++shadedPoints;
      litKernel += kernel;
      litOracle += oracle;
      litWrong += wrong;
    }
  d8test::Check(ambiguous == 0, "ground grid: no grid point sits on a shadow boundary of any direction (the oracle is stable under +-1e-4 m)");
  d8test::Check(mismatches == 0 && litKernel == litOracle,
                "ground grid: sun hours at all 361 points equal the closed-form shadow count (swept footprint of length h / tan el)");
  d8test::Check(shadedPoints > 80 && shadedPoints < 361, "ground grid: a good part of the grid is shaded and a good part is not");
  d8test::Check(litWrong != litOracle, "ground grid decoy: a shadow length of h * tan(el) would give a different total");
  // hand values: only the due-south sun at 45 degrees (the shadow falls on y in [10, 30], x in [0, 10]); then (5, 20) is shaded, (5, 31),
  // (-1, 20) and (5, -5) are lit
  const std::vector<SunSample> south = AuthoredSamples({{180.0, 45.0}});
  d8test::Check(SunHoursAt({5, 20, 0}, {0, 0, 0}, box, south, 1, 1).totalHours == 0 && SunHoursAt({5, 31, 0}, {0, 0, 0}, box, south, 1, 1).totalHours == 1 &&
                    SunHoursAt({-1, 20, 0}, {0, 0, 0}, box, south, 1, 1).totalHours == 1 && SunHoursAt({5, -5, 0}, {0, 0, 0}, box, south, 1, 1).totalHours == 1,
                "ground grid: a 20 m box and the sun due south at 45 degrees shade y 10 to 30 behind it and nothing else");
  // samples per hour scale the hours: 4 lit samples at 4 samples per hour are one hour
  const std::vector<SunSample> four = AuthoredSamples({{180, 45}, {180, 45}, {180, 45}, {180, 45}});
  d8test::Check(Near(SunHoursAt({5, 31, 0}, {0, 0, 0}, box, four, 4, 1).totalHours, 1.0, 1e-12), "ground grid: four samples at four per hour are one hour");
}

// ---- ANALYSIS-0002 ---------------------------------------------------------------------------------------------------------
// Cells on a roof and on facades. Building A: [0,10] x [0,10] x [0,20]. Building B: [20,30] x [0,10] x [0,10], 10 m east of A.
// Sun list: west (az 270) at 20, 60 and 80 degrees, and east (az 90) at 50 degrees.
// B's west wall (normal -x), 5 columns x 10 rows of 2 m x 1 m cells, row centres z = 0.5 .. 9.5. A's east edge top (x = 10, z = 20) throws its
// shadow onto x = 20 at height 20 - 10 tan(el): el 20: 16.4 (whole wall shaded), el 60: 2.68 (cells with centre below 2.68, the rows
// at 0.5, 1.5, 2.5, are shaded), el 80: below zero (none). East sun: the wall faces away (n . dir = -cos(50) < 0). Rows 0 to 2: lit by
// 80 only = 1 h; rows 3 to 9: 60 and 80 = 2 h. Per-surface min 1, max 2, area-weighted mean (3 rows * 1 + 7 rows * 2) / 10 = 1.7.
// A's roof (normal +z): all four samples lit = 4 h (min = max = 4). A's west wall (normal -x): the three west suns lit, the east sun faces
// away = 3 h. A's east wall (normal +x): the east sun (50 degrees): B's top west edge x = 20, z = 10: shadow at x = 10 is at height
// 10 - 10 tan 50 < 0, so lit = 1 h; the west suns face away. Legend over [1, 2] with 5 bins: edges 1, 1.2, ... 2; 1 -> bin 0, 1.5 -> bin 2,
// 2 -> bin 4, 4 (roof) -> clamped to bin 4, 0.5 -> bin 0.
D8_EXTRA_TEST(Rw5FormaSunHoursFacadesRoofs) {
  std::vector<Tri> occ = PrismTriangles({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 0, 20);
  const std::vector<Tri> bBox = PrismTriangles({{20, 0}, {30, 0}, {30, 10}, {20, 10}}, 0, 10);
  occ.insert(occ.end(), bBox.begin(), bBox.end());
  const std::vector<SunSample> sun = AuthoredSamples({{270, 20}, {270, 60}, {270, 80}, {90, 50}});
  auto hoursOf = [&](const std::vector<Cell>& cells) {
    std::vector<double> v;
    for (const Cell& c : cells) v.push_back(SunHoursAt(c.center, c.normal, occ, sun, 1, 1).totalHours);
    return v;
  };
  const std::vector<Cell> bWest = GridCells({20, 0, 0}, {0, 0, 10}, {0, 10, 0}, 10, 5, 1);   // normal -x; u runs up (10 rows), v along y (5 columns)
  const std::vector<Cell> aRoof = GridCells({0, 0, 20}, {10, 0, 0}, {0, 10, 0}, 5, 5, 2);
  const std::vector<Cell> aWest = GridCells({0, 0, 0}, {0, 0, 20}, {0, 10, 0}, 10, 5, 3);
  const std::vector<Cell> aEast = GridCells({10, 0, 0}, {0, 10, 0}, {0, 0, 20}, 5, 10, 4);   // (0,10,0) x (0,0,20) = +x
  d8test::Check(Near(bWest[0].normal.x, -1, 1e-12) && Near(aRoof[0].normal.z, 1, 1e-12) && Near(aEast[0].normal.x, 1, 1e-12), "cells: facade and roof normals point outwards");
  d8test::Check(bWest.size() == 50 && Near(bWest[0].area, 2.0, 1e-12) && Near(aRoof[0].area, 4.0, 1e-12), "cells: 50 cells of 2 m2 on the wall (1 m high, 2 m wide), 25 cells of 4 m2 on the roof");
  const std::vector<double> hb = hoursOf(bWest);
  int rowMismatch = 0;
  for (size_t i = 0; i < hb.size(); ++i) {
    const double zc = bWest[i].center.z;
    int lit = 1;                                   // el 80: shadow height below zero
    if (zc >= 20.0 - 10.0 * std::tan(60.0 * kPi / 180.0)) ++lit;   // el 60
    if (zc >= 20.0 - 10.0 * std::tan(20.0 * kPi / 180.0)) ++lit;   // el 20 (never: 16.4 > 10)
    if (hb[i] != lit) ++rowMismatch;
  }
  d8test::Check(rowMismatch == 0, "facade cells: B's west wall hours follow the shadow line 20 - 10 tan(el) of A's top edge");
  const std::vector<SurfaceStats> sb = StatsBySurface(bWest, hb);
  d8test::Check(sb.size() == 1 && sb[0].minV == 1.0 && sb[0].maxV == 2.0 && Near(sb[0].meanV, 1.7, 1e-12) && sb[0].count == 50,
                "facade cells: min 1 h, max 2 h and area-weighted mean 1.7 h on B's west wall");
  const std::vector<double> hr = hoursOf(aRoof), hw = hoursOf(aWest), he = hoursOf(aEast);
  const std::vector<SurfaceStats> sr = StatsBySurface(aRoof, hr), sw = StatsBySurface(aWest, hw), se = StatsBySurface(aEast, he);
  d8test::Check(sr[0].minV == 4 && sr[0].maxV == 4 && sw[0].minV == 3 && sw[0].maxV == 3 && se[0].minV == 1 && se[0].maxV == 1,
                "surface cells: roof 4 h, A's west wall 3 h (the east sun faces away), A's east wall 1 h (only the east sun)");
  std::vector<Cell> all = bWest;
  all.insert(all.end(), aRoof.begin(), aRoof.end());
  std::vector<double> allv = hb;
  allv.insert(allv.end(), hr.begin(), hr.end());
  const std::vector<SurfaceStats> sa = StatsBySurface(all, allv);
  d8test::Check(sa.size() == 2 && sa[0].surface == 1 && sa[1].surface == 2 && sa[1].minV == 4, "surface cells: stats are kept per surface id");
  Legend lg;
  lg.lo = 1;
  lg.hi = 2;
  lg.bins = 5;
  d8test::Check(Near(lg.Edge(0), 1.0, 1e-12) && Near(lg.Edge(1), 1.2, 1e-12) && Near(lg.Edge(5), 2.0, 1e-12) && lg.BinOf(1.0) == 0 && lg.BinOf(1.5) == 2 &&
                    lg.BinOf(2.0) == 4 && lg.BinOf(4.0) == 4 && lg.BinOf(0.5) == 0,
                "legend: five bins over [1, 2], values outside the range clamp to the end bins");
  // a facade with nothing behind it: only the suns on its own side count. A north-facing cell (normal +y, no occluders at all) is lit by the
  // sun when n . dir > 0, that is for azimuth within 90 degrees of north: of azimuths 0, 45, 100, 135, 180, 260, 315 at 30 degrees the
  // lit ones are 0, 45 and 315 (cos(az) > 0); none of the directions grazes the wall.
  {
    const std::vector<SunSample> ring = AuthoredSamples({{0, 30}, {45, 30}, {100, 30}, {135, 30}, {180, 30}, {260, 30}, {315, 30}});
    const std::vector<Cell> northWall = GridCells({0, 0, 0}, {0, 0, 10}, {10, 0, 0}, 2, 2, 9);   // (0,0,10) x (10,0,0) = (0, 100, 0): +y
    const double h = SunHoursAt(northWall[0].center, northWall[0].normal, {}, ring, 1, 1).totalHours;
    int expectedLit = 0;
    for (double az : {0.0, 45.0, 100.0, 135.0, 180.0, 260.0, 315.0})
      if (std::cos(az * kPi / 180) > 1e-9) ++expectedLit;
    d8test::Check(Near(northWall[0].normal.y, 1, 1e-12) && expectedLit == 3 && h == 3.0, "facade cells: a bare north-facing cell is lit by the three suns within 90 degrees of north, not by the ones behind it");
  }
  // decoy: if the shadow of A on B were ignored every wall cell would read 3 h and min would equal max
  d8test::Check(sb[0].minV != sb[0].maxV, "facade cells decoy: with the shadow ignored the wall would be uniform (3 h everywhere); the measured wall is not");
}

// ---- ANALYSIS-0003 ---------------------------------------------------------------------------------------------------------
// Dates, ranges, named dates and time windows.
//  * Calendar: 2028-02-27 to 2028-03-02 is five dates (leap year); 2026-12-30 to 2026-12-31 is two; one date is one; an end before the
//    start is none.
//  * Day length by the sunrise equation 2 H0 / 15 with cos H0 = -tan(lat) tan(decl) (decl from the Almanac formulation): lat 52 N on the
//    summer solstice = 16.49 h, on the winter solstice = 7.51 h (the sun's declination is stationary there, so noon values hold for the
//    whole day); lat 0 at the March equinox: exactly 12 h whatever the declination. Sampled once a minute: tolerance two samples, 2/60 h.
//  * A range of three dates sums three days; hoursPerDay is the mean day.
//  * Windows: [10, 14) at lat 40 in June is all daylight = 4 h; [0, 3) is dark = 0 h; [13, 15) = 2 h.
//  * Named dates: declination at noon UT: summer solstice 23.4359 (IAU obliquity 23.439291 - 0.0130042 T) within 0.01, winter its
//    negative, equinoxes within 0.1 degree of zero (the equinox instants are at 14:46 UT on 20 March and about 13:05 on 22 September).
D8_EXTRA_TEST(Rw5FormaSunDatesRangesWindows) {
  SunStudy s;
  s.year = 2028; s.month1 = 2; s.day1 = 27; s.month2 = 3; s.day2 = 2;
  d8test::Check(StudyDayCount(s) == 5, "dates: 27 Feb to 2 Mar 2028 is five dates (leap year)");
  s.year = 2027;
  d8test::Check(StudyDayCount(s) == 4, "dates: 27 Feb to 2 Mar 2027 is four dates (no leap day)");
  s.year = 2026; s.month1 = 12; s.day1 = 30; s.month2 = 12; s.day2 = 31;
  d8test::Check(StudyDayCount(s) == 2, "dates: 30 to 31 Dec is two dates");
  s.month2 = 12; s.day2 = 30;
  d8test::Check(StudyDayCount(s) == 1, "dates: one date is one");
  s.day2 = 29;
  d8test::Check(StudyDayCount(s) == 0 && SunSamples(s).empty(), "dates: an end before the start is no dates and no samples");
  int mo = 0, da = 0;
  NamedDateMonthDay(NamedDate::SummerSolstice, mo, da);
  d8test::Check(mo == 6 && da == 21, "named dates: summer solstice is 21 June");

  auto dayLengthOracle = [](double lat, int y, int m, int d) {
    const AlmSun noon = Almanac(lat, 0.0, y, m, d, 12.0);
    const double c = -std::tan(lat * kPi / 180.0) * std::tan(noon.declDeg * kPi / 180.0);
    const double h0 = std::acos(std::max(-1.0, std::min(1.0, c))) * 180.0 / kPi;
    return 2.0 * h0 / 15.0;
  };
  struct Case { double lat; int m, d; };
  const Case cases[] = {{52.0, 6, 21}, {52.0, 12, 21}, {0.0, 3, 20}};
  for (const Case& c : cases) {
    SunStudy q;
    q.latDeg = c.lat; q.lonDeg = 0; q.tzHours = 0; q.year = 2026;
    q.month1 = q.month2 = c.m; q.day1 = q.day2 = c.d;
    q.samplesPerHour = 60;
    const double kernel = DaylightHours(SunSamples(q), 60);
    const double oracle = dayLengthOracle(c.lat, 2026, c.m, c.d);
    d8test::Check(Near(kernel, oracle, 2.0 / 60 + 0.01), "daylight: day length from sampled sun positions equals the sunrise equation (lat 52 June 16.49 h, December 7.51 h, equator 12 h)");
  }
  d8test::Check(Near(dayLengthOracle(52.0, 2026, 6, 21), 16.49, 0.02) && Near(dayLengthOracle(52.0, 2026, 12, 21), 7.51, 0.02), "daylight oracle: lat 52 gives 16.49 h in June and 7.51 h in December (checked by hand)");
  SunStudy r;
  r.latDeg = 52; r.year = 2026; r.month1 = 6; r.day1 = 20; r.month2 = 6; r.day2 = 22; r.samplesPerHour = 60;
  const std::vector<SunSample> rs = SunSamples(r);
  double sumDays = 0;
  for (int d = 20; d <= 22; ++d) sumDays += dayLengthOracle(52.0, 2026, 6, d);
  d8test::Check(rs.size() == 3 * 24 * 60 && Near(DaylightHours(rs, 60), sumDays, 3 * (2.0 / 60 + 0.01)), "range: three dates give three days of samples and the sum of the three day lengths");
  d8test::Check(rs.back().dayIndex == 2 && rs.front().dayIndex == 0, "range: samples carry their date index");

  SunStudy w;
  w.latDeg = 40; w.lonDeg = 0; w.tzHours = 0; w.year = 2026; w.month1 = w.month2 = 6; w.day1 = w.day2 = 21; w.samplesPerHour = 60;
  w.startHour = 10; w.endHour = 14;
  d8test::Check(SunSamples(w).size() == 240 && Near(DaylightHours(SunSamples(w), 60), 4.0, 1e-12), "window: 10:00 to 14:00 in June at 40 N is 240 samples, all daylight = 4 h");
  w.startHour = 0; w.endHour = 3;
  d8test::Check(Near(DaylightHours(SunSamples(w), 60), 0.0, 1e-12) && SunSamples(w).size() == 180, "window: 00:00 to 03:00 is dark = 0 h");
  w.startHour = 13; w.endHour = 15;
  d8test::Check(Near(DaylightHours(SunSamples(w), 60), 2.0, 1e-12), "window: 13:00 to 15:00 = 2 h");
  w.startHour = 9; w.endHour = 10; w.samplesPerHour = 4;
  const std::vector<SunSample> q4 = SunSamples(w);
  d8test::Check(q4.size() == 4 && Near(q4[0].localHour, 9.125, 1e-12) && Near(q4[3].localHour, 9.875, 1e-12), "window: 4 samples per hour are taken at the slot centres 9.125 .. 9.875");

  auto obliq = [](int y, int m, int d) {
    const double T = (JulianDay(y, m, d, 12.0) - 2451545.0) / 36525.0;
    return 23.439291 - 0.0130042 * T;
  };
  const double eps = obliq(2026, 6, 21);
  int m2, d2;
  NamedDateMonthDay(NamedDate::SummerSolstice, m2, d2);
  const double dSum = SunPosition(0, 0, 2026, m2, d2, 12.0).declinationDeg;
  NamedDateMonthDay(NamedDate::WinterSolstice, m2, d2);
  const double dWin = SunPosition(0, 0, 2026, m2, d2, 12.0).declinationDeg;
  NamedDateMonthDay(NamedDate::SpringEquinox, m2, d2);
  const double dSpr = SunPosition(0, 0, 2026, m2, d2, 12.0).declinationDeg;
  NamedDateMonthDay(NamedDate::AutumnEquinox, m2, d2);
  const double dAut = SunPosition(0, 0, 2026, m2, d2, 12.0).declinationDeg;
  d8test::Check(Near(dSum, eps, 0.01) && Near(dWin, -eps, 0.01) && std::fabs(dSpr) < 0.5 && std::fabs(dAut) < 0.5,
                "named dates: declination +-23.436 degrees at the solstices and within 0.5 degree (one day of drift) of zero at the equinoxes");
  d8test::Check(Near(dSum, 23.4359, 0.01), "named dates: the summer solstice declination is the 2026 obliquity 23.4359");
  d8test::Check(!Near(dSum, 23.0, 0.01) && !Near(dSum, 24.0, 0.01), "named dates decoy: a wrong obliquity is rejected by the tolerance");
  d8test::Check(Near(dSum, Almanac(0, 0, 2026, 6, 21, 12.0).declDeg, 0.02) && Near(dWin, Almanac(0, 0, 2026, 12, 21, 12.0).declDeg, 0.02) &&
                    Near(dSpr, Almanac(0, 0, 2026, 3, 20, 12.0).declDeg, 0.02) && Near(dAut, Almanac(0, 0, 2026, 9, 22, 12.0).declDeg, 0.02),
                "named dates: the four declinations agree with the Almanac formulation to 0.02 degree");
}

// ---- ANALYSIS-0004 ---------------------------------------------------------------------------------------------------------
// Shadows at any time. A rectangular block 12 x 8 m, height 30 m. For a sun direction with elevation e and azimuth a the shadow
// outline is the footprint swept by v = -h (dx, dy) / dz (length h / tan e, away from the sun); its area is a b + |vx| b + |vy| a
// (Minkowski sum of a rectangle and a segment); its farthest vertex from the footprint is at h / tan e along the shadow direction.
//  * authored sun directions (exact to 1e-6: the hull snaps coordinates to a 1e-9 grid);
//  * at the SPA sample time with the Almanac position (relative tolerance 0.25 %: the two positions differ by up to 0.03 degree at 40
//    degrees elevation, d(length)/length = d(e) / (sin e cos e) = 0.001);
//  * scrubbing: the shadow tip's bearing turns clockwise through the day (north hemisphere) and the shadow is empty at night.
D8_EXTRA_TEST(Rw5FormaShadowAtTime) {
  const double a = 12, b = 8, h = 30;
  const std::vector<P2> fp = {{0, 0}, {a, 0}, {a, b}, {0, b}};
  struct Dir { double az, el; };
  const Dir dirs[] = {{180, 45}, {90, 30}, {37, 70}, {200, 10}, {270, 55}, {0, 25}, {315, 40}};
  int bad = 0;
  for (const Dir& d : dirs) {
    const V3 s = DirFromAzEl(d.az, d.el);
    const std::vector<P2> sh = ShadowOfPrism(fp, h, s);
    const double L = h / std::tan(d.el * kPi / 180.0);
    const double vx = L * std::sin((d.az + 180.0) * kPi / 180.0), vy = L * std::cos((d.az + 180.0) * kPi / 180.0);
    const double expected = a * b + std::fabs(vx) * b + std::fabs(vy) * a;
    if (!Near(PolygonArea2(sh), expected, 1e-6)) ++bad;   // the hull snaps coordinates to 1e-9: area error below perimeter * 5e-10 < 1e-6
    // along the shadow direction u (away from the sun) the outline reaches L beyond the footprint: max(u . outline) = max(u . footprint) + L
    const double ux = std::sin((d.az + 180.0) * kPi / 180.0), uy = std::cos((d.az + 180.0) * kPi / 180.0);
    double reachOut = -1e18, reachFp = -1e18;
    for (const P2& p : sh) reachOut = std::max(reachOut, p.x * ux + p.y * uy);
    for (const P2& c : fp) reachFp = std::max(reachFp, c.x * ux + c.y * uy);
    if (!Near(reachOut - reachFp, L, 1e-8)) ++bad;
  }
  d8test::Check(bad == 0, "shadow: outline area a b + |vx| b + |vy| a and shadow length h / tan(el) for seven authored sun directions");
  // hand value: sun due south at 45 degrees: shadow 30 m to the north: area 96 + 0 + 30 * 12 = 456
  d8test::Check(Near(PolygonArea2(ShadowOfPrism(fp, h, DirFromAzEl(180, 45))), 96.0 + 360.0, 1e-6), "shadow: sun due south at 45 degrees gives 96 + 30 * 12 = 456 m2");
  d8test::Check(ShadowOfPrism(fp, h, DirFromAzEl(180, -5)).empty() && ShadowOfPrism(fp, h, DirFromAzEl(180, 0)).empty(), "shadow: no shadow outline when the sun is not above the horizon");
  // decoy: tan instead of cot would give a 30 * tan(45) = 30 m shadow only at 45 degrees; at 30 degrees it differs
  d8test::Check(!Near(PolygonArea2(ShadowOfPrism(fp, h, DirFromAzEl(180, 30))), 96.0 + 12.0 * h * std::tan(30.0 * kPi / 180.0), 1.0), "shadow decoy: at 30 degrees h * tan(el) is not the shadow length");

  const SunPos at = SunPosition(39.742476, -105.1786, 2003, 10, 17, 19.0 + 30.0 / 60 + 30.0 / 3600);
  const AlmSun alm = Almanac(39.742476, -105.1786, 2003, 10, 17, 19.0 + 30.0 / 60 + 30.0 / 3600);
  const double Lk = h / std::tan(at.elevationDeg * kPi / 180.0), Lo = h / std::tan(alm.elDeg * kPi / 180.0);
  const std::vector<P2> shDate = ShadowOfPrism(fp, h, at.dir);
  const double vox = Lo * std::sin((alm.azDeg + 180.0) * kPi / 180.0), voy = Lo * std::cos((alm.azDeg + 180.0) * kPi / 180.0);
  const double expectedDate = a * b + std::fabs(vox) * b + std::fabs(voy) * a;
  d8test::Check(Near(Lk, Lo, 0.0025 * Lo) && Near(PolygonArea2(shDate), expectedDate, 0.0025 * expectedDate),
                "shadow at a date and time: length and area at the SPA sample instant agree with the Almanac sun to 0.25 %");
  d8test::Check(Near(Lo, 36.0, 0.5), "shadow at a date and time: a 30 m block at 39.8 degrees elevation throws a 36 m shadow (30 / tan 39.9)");
  // scrubbing through a day: 45 N, 0 E, 21 June, every two hours of local solar time from 06 to 18: bearing of the shadow tip grows
  double prev = -1;
  bool increasing = true;
  for (double hour = 7; hour <= 17.01; hour += 2) {
    const SunPos p = SunPositionLocal(45.0, 0.0, 0.0, 2026, 6, 21, hour);
    const std::vector<P2> sh = ShadowOfPrism(fp, h, p.dir);
    if (sh.empty()) { increasing = false; continue; }
    const double bearing = p.azimuthDeg + 180.0;   // unwrapped: 250 degrees in the morning to 470 in the evening
    if (!(bearing > prev)) increasing = false;
    prev = bearing;
  }
  d8test::Check(increasing, "shadow scrub: the shadow direction turns clockwise from 07:00 to 17:00 on a June day at 45 N");
  d8test::Check(ShadowOfPrism(fp, h, SunPositionLocal(45.0, 0.0, 0.0, 2026, 6, 21, 1.0).dir).empty(), "shadow scrub: no shadow at 01:00");
}

// ---- ANALYSIS-0005 ---------------------------------------------------------------------------------------------------------
// Thresholds. Cells of areas 1, 1, 2, 4 with sun hours 1.5, 2.0, 2.5, 6.0: at least 2.0 h passes areas 1 + 2 + 4 = 7 of 8 = 0.875 (a value
// equal to the minimum passes); at least 2.5: 6/8 = 0.75; at least 6.5: 0; at least 0: 1. On the wall of the facade test (rows of 1 h and
// 2 h, 3 and 7 rows): at least 2 h passes 7 of 10 rows = 0.70; at least 1 h passes everything. Guidance report with two thresholds.
D8_EXTRA_TEST(Rw5FormaSunThresholdShare) {
  std::vector<Cell> cells(4);
  const double areas[] = {1, 1, 2, 4};
  const std::vector<double> vals = {1.5, 2.0, 2.5, 6.0};
  for (int i = 0; i < 4; ++i) cells[i].area = areas[i];
  d8test::Check(Near(AreaShareAtLeast(cells, vals, 2.0), 0.875, 1e-12), "threshold: at least 2 h passes 7 of 8 m2 (equal passes)");
  d8test::Check(Near(AreaShareAtLeast(cells, vals, 2.5), 0.75, 1e-12) && Near(AreaShareAtLeast(cells, vals, 6.5), 0.0, 1e-12) && Near(AreaShareAtLeast(cells, vals, 0.0), 1.0, 1e-12),
                "threshold: at least 2.5 h 0.75, at least 6.5 h none, at least 0 h all");
  d8test::Check(!Near(AreaShareAtLeast(cells, vals, 2.0), 3.0 / 4.0, 1e-9), "threshold decoy: counting cells instead of area (3 of 4) gives a different share");
  const GuidanceReport g = GuidanceCheck(vals, 2.0);
  d8test::Check(g.pass == 3 && g.fail == 1 && g.failing.size() == 1 && g.failing[0] == 0 && Near(g.passShare, 0.75, 1e-12), "threshold: the report lists the failing cell index 0 and 3 of 4 passing");
  std::vector<Cell> wall = GridCells({20, 0, 0}, {0, 0, 10}, {0, 10, 0}, 10, 5, 1);
  std::vector<double> hrs;
  for (const Cell& c : wall) hrs.push_back(c.center.z < 3.0 ? 1.0 : 2.0);
  d8test::Check(Near(AreaShareAtLeast(wall, hrs, 2.0), 0.7, 1e-12) && Near(AreaShareAtLeast(wall, hrs, 1.0), 1.0, 1e-12), "threshold: a wall with 3 rows of 1 h and 7 rows of 2 h: at least 2 h is 70 % of the area");
  Legend lg;
  lg.lo = 0; lg.hi = 4; lg.bins = 4;
  d8test::Check(lg.BinOf(1.5) == 1 && lg.BinOf(2.0) == 2 && lg.BinOf(3.99) == 3 && lg.BinOf(-1) == 0, "threshold legend: [0,4) in four 1 h bins");
}

// ---- ANALYSIS-0006 ---------------------------------------------------------------------------------------------------------
// Vertical sky component on a facade (normal +x) under the CIE overcast sky L(g) = (1 + 2 sin g) / 3.
//  * unobstructed: VSC = 100 (3/14 + 4 / (7 pi)) = 39.6177 % (illuminance on a vertical plane 2/3 (pi/4 + 2/3) over the horizontal 7 pi/9).
//  * an obstruction wall 2000 m long (y in [-1000, 1000]), 5 m thick, front face at x = 30 m, top at z = 30 m: for the azimuth phi off the
//    facade normal its elevation is atan(Hrel cos(phi) / d). VSC = 100 (9 / 7 pi) Int cos(phi) [pi/4 - G(g_o(phi))] / 3 dphi with
//    G(g) = g/2 + sin(2g)/4 - (2/3) cos^3(g), integrated by Simpson's rule with 4000 intervals (the elevation integral is analytic).
//    Truncating the wall at 1000 m changes this by less than 100 * 0.396 * (1 - sin 89 degrees) = 0.006 points.
// The patch quadrature of the code (0.25 x 0.5 degree patches) is tested against it to 0.1 VSC points (measured error below 0.03).
double VscOracle(double d, double hRel) {
  auto G = [](double g) { return g / 2 + std::sin(2 * g) / 4 - (2.0 / 3) * std::pow(std::cos(g), 3); };
  const int N = 4000;
  const double step = kPi / N;
  double s = 0;
  for (int i = 0; i <= N; ++i) {
    const double phi = -kPi / 2 + i * step;
    const double go = hRel > 0 ? std::atan(hRel / d * std::cos(phi)) : 0.0;
    const double f = std::cos(phi) * (kPi / 4 - G(go)) / 3.0;
    const double w = (i == 0 || i == N) ? 1 : (i % 2 ? 4 : 2);
    s += w * f;
  }
  s *= step / 3.0;
  return 100.0 * s / (7.0 * kPi / 9.0);
}
D8_EXTRA_TEST(Rw5FormaVerticalSkyComponent) {
  const V3 n{1, 0, 0};
  const double closed = 100.0 * (3.0 / 14.0 + 4.0 / (7.0 * kPi));
  d8test::Check(Near(closed, 39.6177, 1e-3), "VSC closed form: 3/14 + 4/(7 pi) = 39.6177 %");
  const double open = 100.0 * SkyFactor({0, 0, 1.5}, n, {}, SkyModel::CieOvercast, 360, 720);
  d8test::Check(Near(open, closed, 0.05), "VSC: an unobstructed facade point sees 39.62 % (CIE overcast sky)");
  const double openUniform = 100.0 * SkyFactor({0, 0, 1.5}, n, {}, SkyModel::Uniform, 360, 720);
  d8test::Check(Near(openUniform, 50.0, 0.05) && !Near(openUniform, closed, 5.0),
                "VSC decoy: a uniform sky gives 50 % on a vertical plane (half the sky), not the CIE overcast 39.6 %");
  const std::vector<Tri> wall = PrismTriangles({{30, -1000}, {35, -1000}, {35, 1000}, {30, 1000}}, 0, 30);
  int bad = 0;
  double sumShade = 0;
  for (double z : {1.5, 7.5, 13.5, 19.5, 25.5}) {
    const double k = 100.0 * SkyFactor({0, 0, z}, n, wall, SkyModel::CieOvercast, 360, 720);
    const double o = VscOracle(30.0, 30.0 - z);
    if (!Near(k, o, 0.1)) ++bad;
    sumShade += closed - k;
  }
  d8test::Check(bad == 0, "VSC: five facade heights behind a 30 m wall 30 m away agree with the integral oracle to 0.1 points");
  d8test::Check(Near(VscOracle(30.0, 28.5), 16.23, 0.01) && VscOracle(30.0, 4.5) > 33.0,
                "VSC oracle: 16.23 % at 1.5 m height and above 33 % at 25.5 m (hand-checked values)");
  d8test::Check(sumShade > 50.0, "VSC decoy: the wall takes 70 points off the unobstructed 39.6 % over the five heights in total; an obstruction-blind code would give 0");
  // a far-away obstruction approaches the open value, a near one is worse (monotone)
  const std::vector<Tri> nearWall = PrismTriangles({{10, -1000}, {15, -1000}, {15, 1000}, {10, 1000}}, 0, 30);
  const double vNear = 100.0 * SkyFactor({0, 0, 1.5}, n, nearWall, SkyModel::CieOvercast, 180, 360);
  d8test::Check(Near(vNear, VscOracle(10.0, 28.5), 0.1) && vNear < 100.0 * SkyFactor({0, 0, 1.5}, n, wall, SkyModel::CieOvercast, 180, 360), "VSC: a wall 10 m away gives the integral value and less sky than one 30 m away");
}

// ---- ANALYSIS-0007 ---------------------------------------------------------------------------------------------------------
// Sky view factor on the ground in an infinite street canyon of width W and wall height H, at the centre of the street (Oke 1981):
// SVF = 1 / sqrt(1 + (2 H / W)^2). Two walls 20000 m long, 10 m thick. W = 20: H = 30 -> k = 3 -> 0.316228; H = 10 -> k = 1 -> 0.707107;
// H = 0 -> 1. Truncating the walls at 10000 m each side changes the factor by less than 2/pi * W / L = 0.0006; the quadrature
// (0.5 x 1 degree patches) measured 0.0003; tolerance 0.002. Also a point near one wall sees less than the centre and the courtyard
// (a square well) sees a known fraction: centre of a 20 x 20 m well with 20 m walls: the four walls each subtend a solid angle
// which the cosine-weighted sum 1 - 4 * (form factor of a wall rectangle) gives; here we only check it lies between the canyon cases.
D8_EXTRA_TEST(Rw5FormaSkyViewFactorCanyon) {
  const V3 up{0, 0, 1};
  auto canyon = [](double H) {
    std::vector<Tri> t = PrismTriangles({{-20, -20000}, {-10, -20000}, {-10, 20000}, {-20, 20000}}, 0, H);
    const std::vector<Tri> r = PrismTriangles({{10, -20000}, {20, -20000}, {20, 20000}, {10, 20000}}, 0, H);
    t.insert(t.end(), r.begin(), r.end());
    return t;
  };
  int bad = 0;
  for (double H : {30.0, 10.0, 20.0, 5.0}) {
    const double k = 2.0 * H / 20.0;
    const double oracle = 1.0 / std::sqrt(1.0 + k * k);
    const double svf = SkyFactor({0, 0, 0}, up, canyon(H), SkyModel::Uniform, 180, 360);
    if (!Near(svf, oracle, 0.002)) ++bad;
  }
  d8test::Check(bad == 0, "sky view factor: canyon centre equals 1 / sqrt(1 + (2H/W)^2) for H = 30, 10, 20, 5 at W = 20 (0.002)");
  d8test::Check(Near(SkyFactor({0, 0, 0}, up, {}, SkyModel::Uniform, 180, 360), 1.0, 1e-4), "sky view factor: open ground sees the whole sky = 1");
  const double centre = SkyFactor({0, 0, 0}, up, canyon(30), SkyModel::Uniform, 180, 360);
  // off the centre line, at distances a and b from the two walls: SVF = (a / sqrt(a^2 + H^2) + b / sqrt(b^2 + H^2)) / 2 (each wall covers half
  // the azimuths; the integral over one wall gives pi / sqrt(1 + (H/a)^2)); a = 2, b = 18, H = 30: 0.2905 against 0.3162 at the centre
  const double nearWall = SkyFactor({8, 0, 0}, up, canyon(30), SkyModel::Uniform, 180, 360);
  const double nearOracle = 0.5 * (2.0 / std::sqrt(4.0 + 900.0) + 18.0 / std::sqrt(324.0 + 900.0));
  d8test::Check(Near(nearOracle, 0.2905, 1e-4) && Near(nearWall, nearOracle, 0.002) && nearWall < centre - 0.02,
                "sky view factor: a point 2 m from a wall sees 0.2905, less than the street centre, as the closed form says");
  d8test::Check(!Near(centre, 1.0 - 30.0 / 40.0, 0.02), "sky view factor decoy: the linear guess 1 - H / (W + H) is not the closed form (0.316 versus 0.25)");
  // a square well (courtyard) of 20 x 20 m with 30 m walls: the horizon at azimuth phi is at distance 10 / max(|cos phi|, |sin phi|), its
  // elevation atan(H / that), so SVF = 1/(2 pi) Int cos^2(elevation) dphi, integrated here by Simpson's rule
  std::vector<Tri> well;
  for (const auto& r : std::vector<std::vector<P2>>{{{-20, -20}, {20, -20}, {20, -10}, {-20, -10}}, {{-20, 10}, {20, 10}, {20, 20}, {-20, 20}},
                                                    {{-20, -10}, {-10, -10}, {-10, 10}, {-20, 10}}, {{10, -10}, {20, -10}, {20, 10}, {10, 10}}}) {
    const std::vector<Tri> t = PrismTriangles(r, 0, 30);
    well.insert(well.end(), t.begin(), t.end());
  }
  const double wellSvf = SkyFactor({0, 0, 0}, up, well, SkyModel::Uniform, 180, 360);
  double wellOracle = 0;
  {
    const int N = 20000;
    const double step = 2 * kPi / N;
    for (int i = 0; i <= N; ++i) {
      const double phi = i * step, m = std::max(std::fabs(std::cos(phi)), std::fabs(std::sin(phi)));
      const double f = 1.0 / (1.0 + 30.0 * 30.0 * m * m / 100.0);
      wellOracle += f * ((i == 0 || i == N) ? 1 : (i % 2 ? 4 : 2));
    }
    wellOracle *= step / 3.0 / (2 * kPi);
  }
  d8test::Check(Near(wellSvf, wellOracle, 0.002) && wellSvf < centre, "sky view factor: the centre of a square 20 m well with 30 m walls equals the azimuth integral (and is below the canyon of the same width)");
}

// ---- ANALYSIS-0008 ---------------------------------------------------------------------------------------------------------
// Daylight potential against guidance levels (BRE VSC 27 % is the example level in the row). Facade points at heights 1.5, 7.5, 13.5,
// 19.5, 25.5 m behind a 30 m wall 30 m away: oracle values 16.23, 20.21, 25.07, 30.55 and above 33 %; at the 27 % level the first three
// fail and the last two pass (margins 1.9 points and more against a code error of 0.03); at a 20 % level only the first fails; at 40 %
// all fail; at 0 all pass.
D8_EXTRA_TEST(Rw5FormaDaylightGuidanceLevels) {
  const V3 n{1, 0, 0};
  const std::vector<Tri> wall = PrismTriangles({{30, -1000}, {35, -1000}, {35, 1000}, {30, 1000}}, 0, 30);
  std::vector<double> vsc, oracle;
  for (double z : {1.5, 7.5, 13.5, 19.5, 25.5}) {
    vsc.push_back(100.0 * SkyFactor({0, 0, z}, n, wall, SkyModel::CieOvercast, 180, 360));
    oracle.push_back(VscOracle(30.0, 30.0 - z));
  }
  const GuidanceReport g27 = GuidanceCheck(vsc, 27.0);
  const GuidanceReport o27 = GuidanceCheck(oracle, 27.0);
  d8test::Check(g27.pass == 2 && g27.fail == 3 && g27.failing == std::vector<int>({0, 1, 2}) && o27.failing == g27.failing,
                "guidance 27 %: the three lower facade points fail, the two upper pass, the same as the integral oracle");
  d8test::Check(Near(g27.passShare, 0.4, 1e-12), "guidance 27 %: 40 % of the points pass");
  const GuidanceReport g20 = GuidanceCheck(vsc, 20.0), g40 = GuidanceCheck(vsc, 40.0), g0 = GuidanceCheck(vsc, 0.0);
  d8test::Check(g20.pass == 4 && g20.failing == std::vector<int>({0}) && g40.pass == 0 && g0.pass == 5, "guidance: a selectable level, 20 % lets four pass, 40 % none, 0 all");
  d8test::Check(Near(oracle[0], 16.23, 0.01) && Near(oracle[3], 30.55, 0.01), "guidance oracle: 16.23 % at 1.5 m and 30.55 % at 19.5 m (hand-checked values)");
  d8test::Check(vsc[1] < 27.0 - 5.0 && vsc[3] > 27.0 + 3.0, "guidance decoy: the points on both sides of the level are well away from it, a wrong level would flip the verdicts");
}

}  // namespace
