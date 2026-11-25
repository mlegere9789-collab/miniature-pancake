// Site-analysis solar and sky maths (Forma-style sun hours, shadows, sky view and daylight potential).
//
// Header-only, standard library only. Frame: x east, y north, z up (metres); azimuth is measured from north, clockwise.
//
//  * SunPosition: the public NOAA solar-position equations (Meeus-based): geometric mean longitude and anomaly, equation of the
//    centre, apparent longitude, obliquity, declination, equation of time, true solar time, hour angle, zenith and azimuth. The
//    elevation is geometric (no refraction). Stated accuracy of the method is about 0.01 degree for years 1800 to 2100.
//  * SunSamples: a study as a list of sun directions for a date or date range, a local-time window and a sampling rate.
//  * SunHoursAt: direct sun hours at a point (optionally with a surface normal) against triangle occluders.
//  * SkyFactor: sky visibility seen from a point: cosine-weighted (uniform sky, the sky view factor on a horizontal plane) or CIE
//    overcast sky (vertical sky component, VSC, on a vertical plane), by midpoint quadrature over elevation x azimuth patches.
//  * ShadowOfPrism: ground shadow of a vertical prism for one sun direction.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace dino8 {
namespace kernel {
namespace forma {

constexpr double kPi = 3.14159265358979323846;
inline double Rad(double d) { return d * kPi / 180.0; }
inline double Deg(double r) { return r * 180.0 / kPi; }

struct V3 {
  double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double Len(V3 a) { return std::sqrt(Dot(a, a)); }

struct P2 {
  double x = 0, y = 0;
};

// ------------------------------------------------------------------------------------------------------------------------------
// Calendar
// ------------------------------------------------------------------------------------------------------------------------------
// Days since 1970-01-01 of a proleptic Gregorian date (Hinnant's civil-days algorithm) and the inverse.
inline long DaysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const long yoe = y - era * 400;
  const long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
inline void CivilFromDays(long z, int& y, int& m, int& d) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const long doe = z - era * 146097;
  const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long yy = yoe + era * 400;
  const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const long mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yy + (m <= 2));
}

// Julian day number of a Gregorian date at `utcHours` (any real number of hours, also negative or above 24).
inline double JulianDay(int y, int m, int d, double utcHours) {
  return static_cast<double>(DaysFromCivil(y, m, d)) + 2440587.5 + utcHours / 24.0;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Sun position (NOAA)
// ------------------------------------------------------------------------------------------------------------------------------
struct SunPos {
  double azimuthDeg = 0;        // from north, clockwise
  double elevationDeg = 0;      // geometric, above the horizon
  double declinationDeg = 0;
  double equationOfTimeMin = 0;
  V3 dir;                       // unit vector towards the sun, east / north / up
};

inline V3 DirFromAzEl(double azimuthDeg, double elevationDeg) {
  const double a = Rad(azimuthDeg), e = Rad(elevationDeg);
  return {std::cos(e) * std::sin(a), std::cos(e) * std::cos(a), std::sin(e)};
}

// latitude north positive, longitude east positive (degrees); (y, m, d, utcHours) the UTC instant.
inline SunPos SunPositionAt(double latDeg, double lonDeg, double julianDay) {
  const double T = (julianDay - 2451545.0) / 36525.0;
  double L0 = std::fmod(280.46646 + T * (36000.76983 + T * 0.0003032), 360.0);
  if (L0 < 0) L0 += 360.0;
  const double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
  const double e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
  const double Mr = Rad(M);
  const double C = std::sin(Mr) * (1.914602 - T * (0.004817 + 0.000014 * T)) + std::sin(2 * Mr) * (0.019993 - 0.000101 * T) +
                   std::sin(3 * Mr) * 0.000289;
  const double trueLong = L0 + C;
  const double omega = 125.04 - 1934.136 * T;
  const double lambdaApp = trueLong - 0.00569 - 0.00478 * std::sin(Rad(omega));
  const double eps0 = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
  const double eps = eps0 + 0.00256 * std::cos(Rad(omega));
  const double decl = std::asin(std::sin(Rad(eps)) * std::sin(Rad(lambdaApp)));
  const double y = std::tan(Rad(eps) / 2) * std::tan(Rad(eps) / 2);
  const double L0r = Rad(L0);
  const double eot = 4.0 * Deg(y * std::sin(2 * L0r) - 2 * e * std::sin(Mr) + 4 * e * y * std::sin(Mr) * std::cos(2 * L0r) -
                               0.5 * y * y * std::sin(4 * L0r) - 1.25 * e * e * std::sin(2 * Mr));
  // minutes of the UTC day (the Julian day starts at noon: add one half)
  const double jd05 = julianDay + 0.5;
  const double minutesOfDay = (jd05 - std::floor(jd05)) * 1440.0;
  double tst = std::fmod(minutesOfDay + eot + 4.0 * lonDeg, 1440.0);
  if (tst < 0) tst += 1440.0;
  double ha = tst / 4.0 - 180.0;
  if (ha < -180.0) ha += 360.0;
  const double lat = Rad(latDeg), har = Rad(ha);
  double cosZen = std::sin(lat) * std::sin(decl) + std::cos(lat) * std::cos(decl) * std::cos(har);
  cosZen = std::max(-1.0, std::min(1.0, cosZen));
  const double zen = std::acos(cosZen);
  double az;
  const double den = std::cos(lat) * std::sin(zen);
  if (std::fabs(den) < 1e-12) {
    az = 180.0;
  } else {
    double c = (std::sin(lat) * cosZen - std::sin(decl)) / den;
    c = std::max(-1.0, std::min(1.0, c));
    const double a = Deg(std::acos(c));
    az = ha > 0 ? std::fmod(a + 180.0, 360.0) : std::fmod(540.0 - a, 360.0);
  }
  SunPos p;
  p.azimuthDeg = az;
  p.elevationDeg = 90.0 - Deg(zen);
  p.declinationDeg = Deg(decl);
  p.equationOfTimeMin = eot;
  p.dir = DirFromAzEl(p.azimuthDeg, p.elevationDeg);
  return p;
}

inline SunPos SunPosition(double latDeg, double lonDeg, int y, int m, int d, double utcHours) {
  return SunPositionAt(latDeg, lonDeg, JulianDay(y, m, d, utcHours));
}

// Local clock time -> sun position (tzHours: offset of local time from UTC, east positive).
inline SunPos SunPositionLocal(double latDeg, double lonDeg, double tzHours, int y, int m, int d, double localHours) {
  return SunPosition(latDeg, lonDeg, y, m, d, localHours - tzHours);
}

// ------------------------------------------------------------------------------------------------------------------------------
// Studies: sun samples for a date or date range and a local-time window
// ------------------------------------------------------------------------------------------------------------------------------
struct SunSample {
  int dayIndex = 0;          // 0 for the first date of the range
  double localHour = 0;      // centre of the sample slot
  V3 dir;
  double elevationDeg = 0;
};

struct SunStudy {
  double latDeg = 0, lonDeg = 0, tzHours = 0;
  int year = 2026;
  int month1 = 6, day1 = 21, month2 = 6, day2 = 21;   // inclusive range; equal = one date
  double startHour = 0, endHour = 24;                // local window [start, end)
  int samplesPerHour = 4;
};

// The named dates of the year (nominal: Mar 20, Jun 21, Sep 22, Dec 21).
enum class NamedDate { SpringEquinox, SummerSolstice, AutumnEquinox, WinterSolstice };
inline void NamedDateMonthDay(NamedDate n, int& month, int& day) {
  switch (n) {
    case NamedDate::SpringEquinox: month = 3; day = 20; break;
    case NamedDate::SummerSolstice: month = 6; day = 21; break;
    case NamedDate::AutumnEquinox: month = 9; day = 22; break;
    case NamedDate::WinterSolstice: month = 12; day = 21; break;
  }
}

inline int StudyDayCount(const SunStudy& s) {
  const long a = DaysFromCivil(s.year, s.month1, s.day1), b = DaysFromCivil(s.year, s.month2, s.day2);
  return b >= a ? static_cast<int>(b - a + 1) : 0;
}

// One sample per slot of 1/samplesPerHour hours inside the window, evaluated at the slot centre; slots with the sun below the horizon are
// kept (dir has z <= 0) so that daylight hours can be counted; exposure code skips them.
inline std::vector<SunSample> SunSamples(const SunStudy& s) {
  std::vector<SunSample> out;
  const int days = StudyDayCount(s);
  const int slots = static_cast<int>(std::floor((s.endHour - s.startHour) * s.samplesPerHour + 1e-9));
  const long d0 = DaysFromCivil(s.year, s.month1, s.day1);
  for (int di = 0; di < days; ++di) {
    int y, m, d;
    CivilFromDays(d0 + di, y, m, d);
    for (int k = 0; k < slots; ++k) {
      SunSample q;
      q.dayIndex = di;
      q.localHour = s.startHour + (k + 0.5) / s.samplesPerHour;
      const SunPos p = SunPositionLocal(s.latDeg, s.lonDeg, s.tzHours, y, m, d, q.localHour);
      q.dir = p.dir;
      q.elevationDeg = p.elevationDeg;
      out.push_back(q);
    }
  }
  return out;
}

// Hours of daylight (sun above the horizon) inside the study window, summed over the dates of the range.
inline double DaylightHours(const std::vector<SunSample>& samples, int samplesPerHour) {
  int n = 0;
  for (const auto& q : samples)
    if (q.elevationDeg > 0) ++n;
  return static_cast<double>(n) / samplesPerHour;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Occluders: triangles, prisms
// ------------------------------------------------------------------------------------------------------------------------------
struct Tri {
  V3 a, b, c;
};

inline double PolygonArea2(const std::vector<P2>& p) {  // signed area, counter-clockwise positive (shoelace)
  double a = 0;
  for (size_t i = 0, n = p.size(); i < n; ++i) a += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
  return a / 2.0;
}

// Ear-clipping triangulation of a simple polygon (any orientation); returns index triples of counter-clockwise triangles.
inline std::vector<std::array<int, 3>> TriangulateEarClip(const std::vector<P2>& poly) {
  std::vector<std::array<int, 3>> tris;
  const int n = static_cast<int>(poly.size());
  if (n < 3) return tris;
  std::vector<int> idx(n);
  for (int i = 0; i < n; ++i) idx[i] = i;
  if (PolygonArea2(poly) < 0) std::reverse(idx.begin(), idx.end());
  auto cross = [&](int a, int b, int c) {
    return (poly[b].x - poly[a].x) * (poly[c].y - poly[a].y) - (poly[b].y - poly[a].y) * (poly[c].x - poly[a].x);
  };
  int guard = 0;
  while (idx.size() > 3 && guard++ < 10000) {
    bool clipped = false;
    const int m = static_cast<int>(idx.size());
    for (int i = 0; i < m; ++i) {
      const int a = idx[(i + m - 1) % m], b = idx[i], c = idx[(i + 1) % m];
      if (cross(a, b, c) <= 1e-12) continue;
      bool inside = false;
      for (int j = 0; j < m && !inside; ++j) {
        const int p = idx[j];
        if (p == a || p == b || p == c) continue;
        if (cross(a, b, p) >= -1e-12 && cross(b, c, p) >= -1e-12 && cross(c, a, p) >= -1e-12) inside = true;
      }
      if (inside) continue;
      tris.push_back({a, b, c});
      idx.erase(idx.begin() + i);
      clipped = true;
      break;
    }
    if (!clipped) break;
  }
  if (idx.size() == 3) tris.push_back({idx[0], idx[1], idx[2]});
  return tris;
}

// Closed prism over a footprint between heights z0 and z1: side quads (two triangles each) and both caps.
inline std::vector<Tri> PrismTriangles(const std::vector<P2>& fp, double z0, double z1) {
  std::vector<Tri> out;
  const size_t n = fp.size();
  for (size_t i = 0; i < n; ++i) {
    const P2 &a = fp[i], &b = fp[(i + 1) % n];
    const V3 a0{a.x, a.y, z0}, b0{b.x, b.y, z0}, a1{a.x, a.y, z1}, b1{b.x, b.y, z1};
    out.push_back({a0, b0, b1});
    out.push_back({a0, b1, a1});
  }
  for (const auto& t : TriangulateEarClip(fp)) {
    out.push_back({{fp[t[0]].x, fp[t[0]].y, z1}, {fp[t[1]].x, fp[t[1]].y, z1}, {fp[t[2]].x, fp[t[2]].y, z1}});
    out.push_back({{fp[t[0]].x, fp[t[0]].y, z0}, {fp[t[1]].x, fp[t[1]].y, z0}, {fp[t[2]].x, fp[t[2]].y, z0}});
  }
  return out;
}

// Any hit along origin + t * dir, t > 0 (Moeller-Trumbore, edges and corners inclusive within 1e-12).
inline bool RayHitsAny(V3 o, V3 d, const std::vector<Tri>& tris) {
  for (const Tri& t : tris) {
    const V3 e1 = t.b - t.a, e2 = t.c - t.a;
    const V3 pv = Cross(d, e2);
    const double det = Dot(e1, pv);
    if (std::fabs(det) < 1e-14) continue;
    const double inv = 1.0 / det;
    const V3 tv = o - t.a;
    const double u = Dot(tv, pv) * inv;
    if (u < -1e-12 || u > 1.0 + 1e-12) continue;
    const V3 qv = Cross(tv, e1);
    const double v = Dot(d, qv) * inv;
    if (v < -1e-12 || u + v > 1.0 + 1e-12) continue;
    if (Dot(e2, qv) * inv > 1e-9) return true;
  }
  return false;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Direct sun at a point or surface cell
// ------------------------------------------------------------------------------------------------------------------------------
// Lit samples at `p` with surface normal `n` (zero vector = a ground point that only needs the sun above the horizon and no occluder).
// A sample counts when the sun is above the horizon, faces the surface (n . dir > 0) and the ray from p towards the sun hits nothing.
inline int SunLitSamples(V3 p, V3 n, const std::vector<Tri>& occluders, const std::vector<SunSample>& samples) {
  int lit = 0;
  const bool hasN = Len(n) > 0;
  const V3 o = hasN ? p + n * 1e-6 : p + V3{0, 0, 1e-6};
  for (const SunSample& s : samples) {
    if (s.dir.z <= 0) continue;
    if (hasN && Dot(n, s.dir) <= 0) continue;
    if (RayHitsAny(o, s.dir, occluders)) continue;
    ++lit;
  }
  return lit;
}

struct SunHoursResult {
  double totalHours = 0;   // summed over all dates of the study
  double hoursPerDay = 0;  // total divided by the number of dates (the mean day)
};
inline SunHoursResult SunHoursAt(V3 p, V3 n, const std::vector<Tri>& occluders, const std::vector<SunSample>& samples, int samplesPerHour,
                                 int days) {
  SunHoursResult r;
  r.totalHours = static_cast<double>(SunLitSamples(p, n, occluders, samples)) / samplesPerHour;
  r.hoursPerDay = days > 0 ? r.totalHours / days : 0;
  return r;
}

// A cell of an analysed surface.
struct Cell {
  V3 center, normal;
  double area = 0;
  int surface = 0;
};

// nu x nv cells over the parallelogram origin + s u + t v (s, t in [0,1]); the normal is the unit vector along u x v.
inline std::vector<Cell> GridCells(V3 origin, V3 u, V3 v, int nu, int nv, int surfaceId) {
  std::vector<Cell> out;
  V3 n = Cross(u, v);
  const double len = Len(n);
  const double area = len / (nu * nv);
  n = n * (1.0 / len);
  for (int j = 0; j < nv; ++j)
    for (int i = 0; i < nu; ++i) {
      Cell c;
      c.center = origin + u * ((i + 0.5) / nu) + v * ((j + 0.5) / nv);
      c.normal = n;
      c.area = area;
      c.surface = surfaceId;
      out.push_back(c);
    }
  return out;
}

struct SurfaceStats {
  int surface = 0;
  int count = 0;
  double minV = 0, maxV = 0, meanV = 0;   // meanV is area weighted
  double area = 0;
};
inline std::vector<SurfaceStats> StatsBySurface(const std::vector<Cell>& cells, const std::vector<double>& values) {
  std::vector<SurfaceStats> out;
  for (size_t i = 0; i < cells.size(); ++i) {
    SurfaceStats* s = nullptr;
    for (auto& x : out)
      if (x.surface == cells[i].surface) s = &x;
    if (!s) {
      out.push_back({});
      s = &out.back();
      s->surface = cells[i].surface;
      s->minV = values[i];
      s->maxV = values[i];
    }
    s->count++;
    s->minV = std::min(s->minV, values[i]);
    s->maxV = std::max(s->maxV, values[i]);
    s->meanV += values[i] * cells[i].area;
    s->area += cells[i].area;
  }
  for (auto& s : out)
    if (s.area > 0) s.meanV /= s.area;
  return out;
}

// Legend: `bins` equal bins over [lo, hi]; a value below lo falls in bin 0, at or above hi in the last bin.
struct Legend {
  double lo = 0, hi = 1;
  int bins = 5;
  double Edge(int i) const { return lo + (hi - lo) * i / bins; }
  int BinOf(double v) const {
    if (hi <= lo) return 0;
    int b = static_cast<int>(std::floor((v - lo) / (hi - lo) * bins));
    return std::max(0, std::min(bins - 1, b));
  }
};

// Share of the total area whose value reaches `threshold` (value >= threshold counts).
inline double AreaShareAtLeast(const std::vector<Cell>& cells, const std::vector<double>& values, double threshold) {
  double pass = 0, all = 0;
  for (size_t i = 0; i < cells.size(); ++i) {
    all += cells[i].area;
    if (values[i] >= threshold) pass += cells[i].area;
  }
  return all > 0 ? pass / all : 0;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Sky visibility
// ------------------------------------------------------------------------------------------------------------------------------
enum class SkyModel { Uniform, CieOvercast };

inline double SkyRelativeLuminance(SkyModel m, double elevationRad) {
  return m == SkyModel::Uniform ? 1.0 : (1.0 + 2.0 * std::sin(elevationRad)) / 3.0;
}

// Illuminance on a horizontal plane under the unobstructed sky, in units of the zenith luminance: pi (uniform) or 7 pi / 9 (CIE overcast).
inline double SkyHorizontalIlluminance(SkyModel m) { return m == SkyModel::Uniform ? kPi : 7.0 * kPi / 9.0; }

// Sky component at point p with surface normal n: illuminance from the visible sky over the unobstructed horizontal illuminance of the same
// sky. Horizontal plane + uniform sky = the sky view factor; vertical plane + CIE overcast = the vertical sky component as a fraction
// (times 100 for percent). Midpoint quadrature over nEl x nAz patches of the upper hemisphere with exact patch solid angles.
inline double SkyFactor(V3 p, V3 n, const std::vector<Tri>& occluders, SkyModel model, int nEl = 180, int nAz = 360) {
  const double dEl = (kPi / 2) / nEl, dAz = 2 * kPi / nAz;
  const V3 o = p + n * 1e-6;
  double sum = 0;
  for (int i = 0; i < nEl; ++i) {
    const double e1 = i * dEl, e2 = (i + 1) * dEl, em = (e1 + e2) / 2;
    const double dOmega = (std::sin(e2) - std::sin(e1)) * dAz;
    const double L = SkyRelativeLuminance(model, em);
    for (int j = 0; j < nAz; ++j) {
      const double az = (j + 0.5) * dAz;
      const V3 d{std::cos(em) * std::sin(az), std::cos(em) * std::cos(az), std::sin(em)};
      const double c = Dot(n, d);
      if (c <= 0) continue;
      if (RayHitsAny(o, d, occluders)) continue;
      sum += L * c * dOmega;
    }
  }
  return sum / SkyHorizontalIlluminance(model);
}

// ------------------------------------------------------------------------------------------------------------------------------
// Shadows
// ------------------------------------------------------------------------------------------------------------------------------
// Ground (z = 0) point where the ray from (x, y, z) towards the sun's opposite meets the ground. Requires dir.z > 0.
inline P2 ProjectToGround(V3 p, V3 sunDir) { return {p.x - p.z * sunDir.x / sunDir.z, p.y - p.z * sunDir.y / sunDir.z}; }

inline std::vector<P2> ConvexHull2(std::vector<P2> pts) {
  for (P2& q : pts) {   // snap to a 1e-9 grid so that coordinates equal up to rounding noise compare equal
    q.x = std::round(q.x * 1e9) / 1e9 + 0.0;
    q.y = std::round(q.y * 1e9) / 1e9 + 0.0;
  }
  std::sort(pts.begin(), pts.end(), [](const P2& a, const P2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
  pts.erase(std::unique(pts.begin(), pts.end(), [](const P2& a, const P2& b) { return a.x == b.x && a.y == b.y; }), pts.end());
  if (pts.size() < 3) return pts;
  std::vector<P2> h(2 * pts.size());
  size_t k = 0;
  auto cross = [](const P2& o, const P2& a, const P2& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
  for (size_t i = 0; i < pts.size(); ++i) {
    while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
    h[k++] = pts[i];
  }
  for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
    while (k >= t && cross(h[k - 2], h[k - 1], pts[i - 1]) <= 0) --k;
    h[k++] = pts[i - 1];
  }
  h.resize(k - 1);
  return h;
}

// Outline of the ground shadow of a vertical prism (convex footprint, height `height`, base at z = 0) with the sun in direction sunDir:
// the convex hull of the footprint and of the footprint projected from the roof. Empty when the sun is not above the horizon.
inline std::vector<P2> ShadowOfPrism(const std::vector<P2>& footprint, double height, V3 sunDir) {
  if (sunDir.z <= 0) return {};
  std::vector<P2> pts = footprint;
  for (const P2& q : footprint) pts.push_back(ProjectToGround({q.x, q.y, height}, sunDir));
  return ConvexHull2(pts);
}

// ------------------------------------------------------------------------------------------------------------------------------
// Guidance levels
// ------------------------------------------------------------------------------------------------------------------------------
// Values (for example vertical sky component in percent) against a selectable minimum level: a value passes when it is at least the level.
struct GuidanceReport {
  int pass = 0, fail = 0;
  double passShare = 0;
  std::vector<int> failing;   // indices of the values below the level
};
inline GuidanceReport GuidanceCheck(const std::vector<double>& values, double level) {
  GuidanceReport r;
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i] >= level) {
      ++r.pass;
    } else {
      ++r.fail;
      r.failing.push_back(static_cast<int>(i));
    }
  }
  if (!values.empty()) r.passShare = static_cast<double>(r.pass) / values.size();
  return r;
}

}  // namespace forma
}  // namespace kernel
}  // namespace dino8
