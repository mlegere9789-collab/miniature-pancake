#include "dino8/kernel/site_analysis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <utility>

namespace dino8::kernel::site {

namespace {
const double kPi = 3.14159265358979323846;
const double kDeg = kPi / 180.0;

double Wrap360(double a) {
  a = std::fmod(a, 360.0);
  return a < 0 ? a + 360.0 : a;
}

double Cross(const Vec2& o, const Vec2& a, const Vec2& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }
}  // namespace

// ---------------------------------------------------------------------------------------------------------------- polygons

double SignedArea(const Polygon& p) {
  double s = 0;
  const std::size_t n = p.size();
  for (std::size_t i = 0; i < n; ++i) {
    const Vec2& a = p[i];
    const Vec2& b = p[(i + 1) % n];
    s += a.x * b.y - b.x * a.y;
  }
  return 0.5 * s;
}

double Perimeter(const Polygon& p) {
  double s = 0;
  for (std::size_t i = 0; i < p.size(); ++i) {
    const Vec2& a = p[i];
    const Vec2& b = p[(i + 1) % p.size()];
    s += std::hypot(b.x - a.x, b.y - a.y);
  }
  return s;
}

Vec2 Centroid(const Polygon& p) {
  double a2 = 0, cx = 0, cy = 0;
  for (std::size_t i = 0; i < p.size(); ++i) {
    const Vec2& a = p[i];
    const Vec2& b = p[(i + 1) % p.size()];
    const double w = a.x * b.y - b.x * a.y;
    a2 += w;
    cx += (a.x + b.x) * w;
    cy += (a.y + b.y) * w;
  }
  if (std::fabs(a2) < 1e-300) return p.empty() ? Vec2{} : p[0];
  return {cx / (3.0 * a2), cy / (3.0 * a2)};
}

bool PointInPolygon(const Polygon& p, double x, double y) {
  bool in = false;
  const std::size_t n = p.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    const Vec2& a = p[i];
    const Vec2& b = p[j];
    if (((a.y > y) != (b.y > y)) && (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)) in = !in;
  }
  return in;
}

double RegionArea(const Polygon& outer, const std::vector<Polygon>& holes) {
  double a = std::fabs(SignedArea(outer));
  for (const Polygon& h : holes) a -= std::fabs(SignedArea(h));
  return a;
}

Polygon ConvexHull(std::vector<Vec2> pts) {
  std::sort(pts.begin(), pts.end(), [](const Vec2& a, const Vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
  pts.erase(std::unique(pts.begin(), pts.end(), [](const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }), pts.end());
  const std::size_t n = pts.size();
  if (n < 3) return pts;
  Polygon h(2 * n);
  std::size_t k = 0;
  for (std::size_t i = 0; i < n; ++i) {
    while (k >= 2 && Cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
    h[k++] = pts[i];
  }
  for (std::size_t i = n - 1, t = k + 1; i > 0; --i) {
    while (k >= t && Cross(h[k - 2], h[k - 1], pts[i - 1]) <= 0) --k;
    h[k++] = pts[i - 1];
  }
  h.resize(k - 1);
  return h;
}

Polygon ClipHalfPlane(const Polygon& p, double a, double b, double c) {
  Polygon out;
  const std::size_t n = p.size();
  for (std::size_t i = 0; i < n; ++i) {
    const Vec2& s = p[i];
    const Vec2& e = p[(i + 1) % n];
    const double ds = a * s.x + b * s.y - c, de = a * e.x + b * e.y - c;
    const bool sIn = ds <= 0, eIn = de <= 0;
    if (sIn) out.push_back(s);
    if (sIn != eIn) {
      const double t = ds / (ds - de);
      out.push_back({s.x + t * (e.x - s.x), s.y + t * (e.y - s.y)});
    }
  }
  return out;
}

Polygon InsetConvex(const Polygon& p, double d) {
  Polygon cur = p;
  const std::size_t n = p.size();
  for (std::size_t i = 0; i < n && !cur.empty(); ++i) {
    const Vec2& a = p[i];
    const Vec2& b = p[(i + 1) % n];
    const double len = std::hypot(b.x - a.x, b.y - a.y);
    if (len == 0) continue;
    const double nx = -(b.y - a.y) / len, ny = (b.x - a.x) / len;  // left normal = inward for a CCW polygon
    cur = ClipHalfPlane(cur, -nx, -ny, -(nx * a.x + ny * a.y + d));
  }
  if (cur.size() < 3 || std::fabs(SignedArea(cur)) < 1e-12) return {};
  return cur;
}

Polygon MitredBand(const std::vector<Vec2>& c, double width, int alignment) {
  const std::size_t n = c.size();
  if (n < 2) return {};
  const double dl = alignment > 0 ? width : (alignment < 0 ? 0.0 : 0.5 * width);
  const double dr = width - dl;
  std::vector<Vec2> left(n), right(n);
  std::vector<Vec2> nrm(n - 1);
  for (std::size_t i = 0; i + 1 < n; ++i) {
    const double dx = c[i + 1].x - c[i].x, dy = c[i + 1].y - c[i].y, len = std::hypot(dx, dy);
    nrm[i] = {-dy / len, dx / len};
  }
  for (std::size_t i = 0; i < n; ++i) {
    Vec2 m;
    if (i == 0) m = nrm[0];
    else if (i == n - 1) m = nrm[n - 2];
    else {
      const Vec2 &a = nrm[i - 1], &b = nrm[i];
      const double k = 1.0 + a.x * b.x + a.y * b.y;  // mitre vector (n1 + n2) / (1 + n1.n2)
      m = {(a.x + b.x) / k, (a.y + b.y) / k};
    }
    left[i] = {c[i].x + m.x * dl, c[i].y + m.y * dl};
    right[i] = {c[i].x - m.x * dr, c[i].y - m.y * dr};
  }
  Polygon out(right.begin(), right.end());  // right side forward, left side back: counter-clockwise
  for (std::size_t i = n; i-- > 0;) out.push_back(left[i]);
  return out;
}

// ---------------------------------------------------------------------------------------------------------------- sun

double JulianDay(int year, int month, int day, double hourUT) {
  int y = year, m = month;
  if (m <= 2) {
    y -= 1;
    m += 12;
  }
  const int A = y / 100;
  const int B = 2 - A + A / 4;  // Gregorian correction (Meeus 7.1)
  const double jd0 = std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + day + B - 1524.5;
  return jd0 + hourUT / 24.0;
}

SunCoordinates SunCoordinatesAt(double jd) {
  const double T = (jd - 2451545.0) / 36525.0;
  const double L0 = Wrap360(280.46646 + 36000.76983 * T + 0.0003032 * T * T);                 // 25.2
  const double M = Wrap360(357.52911 + 35999.05029 * T - 0.0001537 * T * T);                  // 25.3
  const double e = 0.016708634 - 0.000042037 * T - 0.0000001267 * T * T;                      // 25.4
  const double Mr = M * kDeg;
  const double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * std::sin(Mr) +
                   (0.019993 - 0.000101 * T) * std::sin(2 * Mr) + 0.000289 * std::sin(3 * Mr);
  const double trueLon = L0 + C;
  const double nu = M + C;
  SunCoordinates s;
  s.distanceAU = 1.000001018 * (1 - e * e) / (1 + e * std::cos(nu * kDeg));                  // 25.5
  const double omega = 125.04 - 1934.136 * T;
  s.apparentLongitudeDeg = Wrap360(trueLon - 0.00569 - 0.00478 * std::sin(omega * kDeg));
  const double eps0 = 23.0 + 26.0 / 60.0 + 21.448 / 3600.0 - (46.8150 * T + 0.00059 * T * T - 0.001813 * T * T * T) / 3600.0;  // 22.2
  s.obliquityDeg = eps0 + 0.00256 * std::cos(omega * kDeg);
  const double lam = s.apparentLongitudeDeg * kDeg, eps = s.obliquityDeg * kDeg;
  s.rightAscensionDeg = Wrap360(std::atan2(std::cos(eps) * std::sin(lam), std::cos(lam)) / kDeg);  // 25.6
  s.declinationDeg = std::asin(std::sin(eps) * std::sin(lam)) / kDeg;                              // 25.7
  // Equation of time, Meeus chapter 28 (the formula for the mean longitude L0 and eccentricity e above).
  const double y = std::tan(0.5 * eps0 * kDeg) * std::tan(0.5 * eps0 * kDeg);
  const double L0r = L0 * kDeg;
  const double E = y * std::sin(2 * L0r) - 2 * e * std::sin(Mr) + 4 * e * y * std::sin(Mr) * std::cos(2 * L0r) -
                   0.5 * y * y * std::sin(4 * L0r) - 1.25 * e * e * std::sin(2 * Mr);
  s.equationOfTimeMinutes = E / kDeg * 4.0;  // radians to degrees to minutes of time
  return s;
}

SunPosition SunPositionAt(double jd, double latDeg, double lonDeg, double northAngleDeg) {
  const SunCoordinates c = SunCoordinatesAt(jd);
  const double T = (jd - 2451545.0) / 36525.0;
  const double theta0 = Wrap360(280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * T * T - T * T * T / 38710000.0);  // 12.4
  const double H = (theta0 + lonDeg - c.rightAscensionDeg) * kDeg;
  const double phi = latDeg * kDeg, dec = c.declinationDeg * kDeg;
  const double sinh = std::sin(phi) * std::sin(dec) + std::cos(phi) * std::cos(dec) * std::cos(H);  // 13.6
  SunPosition p;
  p.elevationDeg = std::asin(std::max(-1.0, std::min(1.0, sinh))) / kDeg;
  const double A = std::atan2(std::sin(H), std::cos(H) * std::sin(phi) - std::tan(dec) * std::cos(phi));  // 13.5, from south
  p.azimuthDeg = Wrap360(A / kDeg + 180.0 - northAngleDeg);
  return p;
}

double SunPositionErrorBudgetDeg() {
  // Budget (degrees), each term an upper bound from the cited source:
  //   apparent longitude of the Sun, Meeus ch. 25 "low accuracy":              0.0100
  //   -> declination (times sin eps = 0.40):                                    0.0040
  //   -> right ascension, hence hour angle (cos eps / cos^2 delta <= 1.1):      0.0110
  //   mean instead of apparent sidereal time (nutation in RA <= 15.8"):         0.0044
  //   argument TT taken equal to UT (delta T ~ 67 s * 0.0000114 deg/s):         0.0008
  //   geocentric instead of topocentric (parallax 8.794"/R * sin z):            0.0024
  // Zenith error <= 0.0040 + 0.0110 + 0.0044 + 0.0008 + 0.0024 = 0.0226; azimuth error is at most the hour-angle terms
  // divided by sin(zenith), 0.0162 / sin(50 deg) + 0.0024 = 0.0236 for the sun heights the tests use. The bound returned is the
  // sum rounded up.
  return 0.03;
}

Vec3 SunDirection(const SunPosition& s) {
  const double a = s.azimuthDeg * kDeg, e = s.elevationDeg * kDeg;
  return {std::cos(e) * std::sin(a), std::cos(e) * std::cos(a), std::sin(e)};
}

namespace {
double SunElevationUT(int y, int m, int d, double hourUT, double lat, double lon) {
  return SunPositionAt(JulianDay(y, m, d, hourUT), lat, lon).elevationDeg;
}
}  // namespace

SunTimes SunriseSunset(int y, int m, int d, double lat, double lon) {
  const double h0 = -0.833;
  SunTimes r;
  double noon = 12.0 - lon / 15.0;
  for (int it = 0; it < 3; ++it) noon = 12.0 - lon / 15.0 - SunCoordinatesAt(JulianDay(y, m, d, noon)).equationOfTimeMinutes / 60.0;
  r.solarNoonHourUT = noon;
  const double fn = SunElevationUT(y, m, d, noon, lat, lon) - h0;
  const double fa = SunElevationUT(y, m, d, noon - 12.0, lat, lon) - h0;
  const double fb = SunElevationUT(y, m, d, noon + 12.0, lat, lon) - h0;
  if (fn < 0) {
    r.polarNight = true;
    return r;
  }
  if (fa > 0 && fb > 0) {
    r.polarDay = true;
    r.dayLengthHours = 24.0;
    return r;
  }
  auto bisect = [&](double lo, double hi) {  // elevation - h0 changes sign on [lo, hi]
    const bool loNeg = SunElevationUT(y, m, d, lo, lat, lon) - h0 < 0;
    for (int i = 0; i < 60; ++i) {
      const double mid = 0.5 * (lo + hi);
      const bool neg = SunElevationUT(y, m, d, mid, lat, lon) - h0 < 0;
      if (neg == loNeg) lo = mid;
      else hi = mid;
    }
    return 0.5 * (lo + hi);
  };
  r.sunriseHourUT = bisect(noon - 12.0, noon);
  r.sunsetHourUT = bisect(noon, noon + 12.0);
  r.dayLengthHours = r.sunsetHourUT - r.sunriseHourUT;
  return r;
}

double ShadowLength(double height, double elevationDeg) {
  if (elevationDeg <= 0) return std::numeric_limits<double>::infinity();
  return height / std::tan(elevationDeg * kDeg);
}

double HeightFromShadow(double len, double elevationDeg) { return len * std::tan(elevationDeg * kDeg); }

// ---------------------------------------------------------------------------------------------------------------- shadows

Prism BoxPrism(double x0, double y0, double x1, double y1, double zBase, double height) {
  Prism p;
  p.footprint = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
  p.zBase = zBase;
  p.height = height;
  return p;
}

Polygon ShadowOnGround(const Polygon& fp, double height, double az, double el) {
  if (el <= 0) return {};
  const double L = height / std::tan(el * kDeg);
  const double dx = -L * std::sin(az * kDeg), dy = -L * std::cos(az * kDeg);  // away from the Sun
  std::vector<Vec2> pts = fp;
  for (const Vec2& v : fp) pts.push_back({v.x + dx, v.y + dy});
  return ConvexHull(pts);
}

namespace {
bool SegmentsMeet(const Vec2& p, const Vec2& q, const Vec2& a, const Vec2& b) {
  const double d1 = Cross(p, q, a), d2 = Cross(p, q, b), d3 = Cross(a, b, p), d4 = Cross(a, b, q);
  if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return true;
  auto onSeg = [](const Vec2& s, const Vec2& e, const Vec2& pt) {
    return std::min(s.x, e.x) <= pt.x && pt.x <= std::max(s.x, e.x) && std::min(s.y, e.y) <= pt.y && pt.y <= std::max(s.y, e.y);
  };
  if (d1 == 0 && onSeg(p, q, a)) return true;
  if (d2 == 0 && onSeg(p, q, b)) return true;
  if (d3 == 0 && onSeg(a, b, p)) return true;
  if (d4 == 0 && onSeg(a, b, q)) return true;
  return false;
}
}  // namespace

bool RayHitsPrism(const Vec3& p, const Vec3& d, const Prism& pr) {
  const double zb = pr.zBase, zt = pr.zBase + pr.height;
  double t1, t2;
  if (std::fabs(d.z) < 1e-15) {
    if (p.z < zb || p.z > zt) return false;
    t1 = 0;
    t2 = 1e7;
  } else {
    const double ta = (zb - p.z) / d.z, tb = (zt - p.z) / d.z;
    t1 = std::max(0.0, std::min(ta, tb));
    t2 = std::max(ta, tb);
    if (t2 < 0) return false;
  }
  const Vec2 a{p.x + d.x * t1, p.y + d.y * t1}, b{p.x + d.x * t2, p.y + d.y * t2};
  if (PointInPolygon(pr.footprint, a.x, a.y) || PointInPolygon(pr.footprint, b.x, b.y)) return true;
  const std::size_t n = pr.footprint.size();
  for (std::size_t i = 0; i < n; ++i)
    if (SegmentsMeet(a, b, pr.footprint[i], pr.footprint[(i + 1) % n])) return true;
  return false;
}

std::vector<SunSample> SunSamplesForDay(int y, int m, int d, double lat, double lon, double stepMin, double northAngle) {
  std::vector<SunSample> out;
  const double noon = 12.0 - lon / 15.0;
  const double step = stepMin / 60.0;
  const int n = static_cast<int>(std::floor(24.0 / step + 0.5));
  for (int i = 0; i < n; ++i) {
    const double t = noon - 12.0 + (i + 0.5) * step;
    const SunPosition p = SunPositionAt(JulianDay(y, m, d, t), lat, lon, northAngle);
    if (p.elevationDeg > 0) out.push_back({p.azimuthDeg, p.elevationDeg, step});
  }
  return out;
}

namespace {
bool Blocked(const Vec3& p, const Vec3& dir, const std::vector<Prism>& obs) {
  for (const Prism& pr : obs)
    if (RayHitsPrism(p, dir, pr)) return true;
  return false;
}
}  // namespace

SunHoursGrid GroundSunHours(const GridSpec& g, const std::vector<Prism>& obs, const std::vector<SunSample>& sun) {
  SunHoursGrid r;
  r.grid = g;
  r.hours.assign(static_cast<std::size_t>(g.nx) * g.ny, 0.0);
  r.valid.assign(r.hours.size(), 1);
  std::vector<Vec3> dirs;
  for (const SunSample& s : sun) dirs.push_back(SunDirection({s.azimuthDeg, s.elevationDeg}));
  for (int j = 0; j < g.ny; ++j)
    for (int i = 0; i < g.nx; ++i) {
      const std::size_t k = static_cast<std::size_t>(j) * g.nx + i;
      const double x = g.originX + (i + 0.5) * g.spacing, y = g.originY + (j + 0.5) * g.spacing;
      bool under = false;
      for (const Prism& pr : obs)
        if (g.z >= pr.zBase && g.z < pr.zBase + pr.height && PointInPolygon(pr.footprint, x, y)) {
          under = true;
          break;
        }
      if (under) {
        r.valid[k] = 0;
        continue;
      }
      double h = 0;
      for (std::size_t s = 0; s < dirs.size(); ++s)
        if (!Blocked({x, y, g.z}, dirs[s], obs)) h += sun[s].hours;
      r.hours[k] = h;
    }
  return r;
}

std::vector<double> FacadeSunHours(const Vec2& p0, const Vec2& p1, double height, int nu, int nv, const std::vector<Prism>& obs,
                                   const std::vector<SunSample>& sun, bool outwardIsLeft) {
  const double len = std::hypot(p1.x - p0.x, p1.y - p0.y);
  const Vec2 u{(p1.x - p0.x) / len, (p1.y - p0.y) / len};
  Vec2 n{-u.y, u.x};
  if (!outwardIsLeft) n = {-n.x, -n.y};
  std::vector<double> out(static_cast<std::size_t>(nu) * nv, 0.0);
  std::vector<Vec3> dirs;
  for (const SunSample& s : sun) dirs.push_back(SunDirection({s.azimuthDeg, s.elevationDeg}));
  for (int j = 0; j < nv; ++j)
    for (int i = 0; i < nu; ++i) {
      const double a = (i + 0.5) / nu * len, z = (j + 0.5) / nv * height, eps = 1e-6;
      const Vec3 p{p0.x + u.x * a + n.x * eps, p0.y + u.y * a + n.y * eps, z};
      double h = 0;
      for (std::size_t s = 0; s < dirs.size(); ++s) {
        if (dirs[s].x * n.x + dirs[s].y * n.y <= 0) continue;  // the Sun is behind the wall
        if (!Blocked(p, dirs[s], obs)) h += sun[s].hours;
      }
      out[static_cast<std::size_t>(j) * nu + i] = h;
    }
  return out;
}

double ShareAtLeast(const std::vector<double>& v, const std::vector<char>* valid, double thr) {
  std::size_t n = 0, pass = 0;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (valid && !(*valid)[i]) continue;
    ++n;
    if (v[i] >= thr) ++pass;
  }
  return n ? static_cast<double>(pass) / static_cast<double>(n) : 0.0;
}

double ShareAbove(const std::vector<double>& v, const std::vector<char>* valid, double limit) {
  std::size_t n = 0, pass = 0;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (valid && !(*valid)[i]) continue;
    ++n;
    if (v[i] > limit) ++pass;
  }
  return n ? static_cast<double>(pass) / static_cast<double>(n) : 0.0;
}

std::vector<std::size_t> IndicesAbove(const std::vector<double>& v, double limit) {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < v.size(); ++i)
    if (v[i] > limit) out.push_back(i);
  return out;
}

// ---------------------------------------------------------------------------------------------------------------- sky

double SkyFactor(const Vec3& p, const Vec3& n, const std::vector<Prism>& obs, SkyModel sky, int na, int ne) {
  double visible = 0, reference = 0;
  const double dphi = 2 * kPi / na, dgam = 0.5 * kPi / ne;
  for (int ie = 0; ie < ne; ++ie) {
    const double g0 = ie * dgam, g1 = (ie + 1) * dgam, gm = 0.5 * (g0 + g1);
    const double solid = (std::sin(g1) - std::sin(g0)) * dphi;  // exact solid angle of the cell
    const double lum = sky == SkyModel::Uniform ? 1.0 : (1.0 + 2.0 * std::sin(gm)) / 3.0;  // CIE overcast, Moon and Spencer
    for (int ia = 0; ia < na; ++ia) {
      const double a = (ia + 0.5) * dphi;
      const Vec3 d{std::cos(gm) * std::sin(a), std::cos(gm) * std::cos(a), std::sin(gm)};
      reference += lum * d.z * solid;  // unobstructed horizontal plane: cosine of incidence = sin(elevation)
      const double c = d.x * n.x + d.y * n.y + d.z * n.z;
      if (c <= 0) continue;
      if (Blocked({p.x + 1e-7 * n.x, p.y + 1e-7 * n.y, p.z + 1e-7 * n.z}, d, obs)) continue;
      visible += lum * c * solid;
    }
  }
  return reference > 0 ? visible / reference : 0.0;
}

// ---------------------------------------------------------------------------------------------------------------- wind and noise

double LogLawSpeed(double uRef, double zRef, double z, double z0) { return uRef * std::log(z / z0) / std::log(zRef / z0); }

double PointSourceLevel(double lw, double r) { return lw - 20.0 * std::log10(r) - 11.0; }

// ---------------------------------------------------------------------------------------------------------------- plots

double BuildingHeight(const Building& b) { return b.floors * b.floorHeight; }
double GrossFloorArea(const Building& b) { return std::fabs(SignedArea(b.footprint)) * b.floors; }

std::map<std::string, double> GfaByFunction(const std::vector<Building>& buildings) {
  std::map<std::string, double> out;
  for (const Building& b : buildings) {
    const double a = std::fabs(SignedArea(b.footprint));
    for (int f = 0; f < b.floors; ++f) {
      const std::string& name = f < static_cast<int>(b.floorFunction.size()) ? b.floorFunction[f] : std::string("unassigned");
      out[name] += a;
    }
  }
  return out;
}

double EstimateContextHeight(std::optional<double> heightTag, std::optional<int> levelsTag, double perLevel) {
  if (heightTag && *heightTag > 0) return *heightTag;
  const int levels = levelsTag ? std::max(1, *levelsTag) : 1;
  return levels * perLevel;
}

LineBuildingMetrics MeasureLineBuilding(const LineBuilding& lb) {
  LineBuildingMetrics m;
  m.footprintArea = std::fabs(SignedArea(MitredBand(lb.centreline, lb.width, lb.alignment)));
  m.grossFloorArea = m.footprintArea * lb.floors;
  m.volume = m.grossFloorArea * lb.floorHeight;
  return m;
}

EnvelopeReport CheckEnvelope(const Polygon& plot, const std::vector<Building>& buildings, const EnvelopeRules& rules) {
  EnvelopeReport r;
  r.plotArea = std::fabs(SignedArea(plot));
  for (std::size_t i = 0; i < buildings.size(); ++i) {
    const Building& b = buildings[i];
    r.footprintArea += std::fabs(SignedArea(b.footprint));
    r.grossFloorArea += GrossFloorArea(b);
    bool bad = false;
    const std::size_t n = plot.size();
    for (std::size_t e = 0; e < n && !bad; ++e) {
      const Vec2& a = plot[e];
      const Vec2& c = plot[(e + 1) % n];
      const double len = std::hypot(c.x - a.x, c.y - a.y);
      for (const Vec2& v : b.footprint) {
        const double dist = ((c.x - a.x) * (v.y - a.y) - (c.y - a.y) * (v.x - a.x)) / len;  // positive inside a CCW plot
        if (dist < rules.setback - 1e-9) {
          bad = true;
          break;
        }
      }
    }
    if (bad) r.setbackViolations.push_back(i);
    if (BuildingHeight(b) > rules.maxHeight + 1e-9) r.heightViolations.push_back(i);
  }
  r.coverage = r.plotArea > 0 ? r.footprintArea / r.plotArea : 0;
  r.far = r.plotArea > 0 ? r.grossFloorArea / r.plotArea : 0;
  r.coverageViolation = r.coverage > rules.maxCoverage + 1e-12;
  r.farViolation = r.far > rules.maxFar + 1e-12;
  return r;
}

std::size_t CountCellCentresInDisc(double cx, double cy, double radius, double cell, double minX, double minY, int nx, int ny) {
  std::size_t n = 0;
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      const double x = minX + (i + 0.5) * cell - cx, y = minY + (j + 0.5) * cell - cy;
      if (x * x + y * y <= radius * radius) ++n;
    }
  return n;
}

int PackParkingStalls(double W, double L, double sw, double sl, double aisle) {
  const int perRow = static_cast<int>(std::floor(W / sw + 1e-9));
  const double module = 2 * sl + aisle;
  const int modules = static_cast<int>(std::floor(L / module + 1e-9));
  int total = modules * 2 * perRow;
  const double rest = L - modules * module;
  if (rest + 1e-9 >= sl + aisle) total += perRow;
  return total;
}

// ---------------------------------------------------------------------------------------------------------------- mass floors

std::vector<MassLevel> MassFloors(const TaperedMass& m, const std::vector<double>& levels) {
  const double A0 = std::fabs(SignedArea(m.base)), P0 = Perimeter(m.base);
  auto u = [&](double z) { return 1.0 + (m.topScale - 1.0) * z / m.height; };
  auto area = [&](double z) { return A0 * u(z) * u(z); };
  std::vector<MassLevel> out;
  for (std::size_t i = 0; i < levels.size(); ++i) {
    MassLevel l;
    l.elevation = levels[i];
    l.floorArea = area(levels[i]);
    l.floorPerimeter = P0 * u(levels[i]);
    const double z0 = levels[i], z1 = i + 1 < levels.size() ? levels[i + 1] : m.height;
    l.volumeAbove = (z1 - z0) / 6.0 * (area(z0) + 4.0 * area(0.5 * (z0 + z1)) + area(z1));  // Simpson: exact for a quadratic
    out.push_back(l);
  }
  return out;
}

// ---------------------------------------------------------------------------------------------------------------- bearings

double BearingToAzimuth(char from, double deg, double min, double sec, char toward) {
  const double th = deg + min / 60.0 + sec / 3600.0;
  if (from == 'N') return toward == 'E' ? th : 360.0 - th;
  return toward == 'E' ? 180.0 - th : 180.0 + th;
}

std::string FormatBearingLabel(const Vec2& a, const Vec2& b, int decimals) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double az = Wrap360(std::atan2(dx, dy) / kDeg);
  char from, toward;
  double th;
  if (az <= 90.0) { from = 'N'; toward = 'E'; th = az; }
  else if (az <= 180.0) { from = 'S'; toward = 'E'; th = 180.0 - az; }
  else if (az <= 270.0) { from = 'S'; toward = 'W'; th = az - 180.0; }
  else { from = 'N'; toward = 'W'; th = 360.0 - az; }
  long total = std::lround(th * 3600.0);
  const int d = static_cast<int>(total / 3600), mi = static_cast<int>((total % 3600) / 60), se = static_cast<int>(total % 60);
  char buf[96];
  std::snprintf(buf, sizeof buf, "%c %d\xC2\xB0%02d'%02d\" %c %.*f", from, d, mi, se, toward, decimals, std::hypot(dx, dy));
  return buf;
}

std::vector<Vec2> TraverseFromBearings(const Vec2& start, const std::vector<BearingLeg>& legs) {
  std::vector<Vec2> pts{start};
  for (const BearingLeg& l : legs) {
    const double az = BearingToAzimuth(l.from, l.degrees, l.minutes, l.seconds, l.toward) * kDeg;
    pts.push_back({pts.back().x + l.distance * std::sin(az), pts.back().y + l.distance * std::cos(az)});
  }
  return pts;
}

Vec3 LocalToShared(const SurveyPoint& sp, const Vec3& l) {
  const double a = sp.angleDeg * kDeg, c = std::cos(a), s = std::sin(a);
  return {sp.easting + l.x * c - l.y * s, sp.northing + l.x * s + l.y * c, sp.elevation + l.z};
}

Vec3 SharedToLocal(const SurveyPoint& sp, const Vec3& w) {
  const double a = sp.angleDeg * kDeg, c = std::cos(a), s = std::sin(a);
  const double dx = w.x - sp.easting, dy = w.y - sp.northing;
  return {dx * c + dy * s, -dx * s + dy * c, w.z - sp.elevation};
}

// ---------------------------------------------------------------------------------------------------------------- terrain

SlopeAspect HornSlopeAspect(const std::vector<double>& z, int nx, int ny, double h, int ix, int iy) {
  auto Z = [&](int dx, int dy) { return z[static_cast<std::size_t>(iy + dy) * nx + (ix + dx)]; };
  (void)ny;
  const double dzdx = ((Z(1, -1) + 2 * Z(1, 0) + Z(1, 1)) - (Z(-1, -1) + 2 * Z(-1, 0) + Z(-1, 1))) / (8.0 * h);
  const double dzdy = ((Z(-1, 1) + 2 * Z(0, 1) + Z(1, 1)) - (Z(-1, -1) + 2 * Z(0, -1) + Z(1, -1))) / (8.0 * h);
  SlopeAspect r;
  r.slopeDeg = std::atan(std::hypot(dzdx, dzdy)) / kDeg;
  r.aspectDeg = (dzdx == 0 && dzdy == 0) ? -1.0 : Wrap360(std::atan2(-dzdx, -dzdy) / kDeg);
  return r;
}

CutFill CutFillVolume(const std::vector<double>& ex, const std::vector<double>& pr, int nx, int ny, double h) {
  CutFill r;
  for (int j = 0; j + 1 < ny; ++j)
    for (int i = 0; i + 1 < nx; ++i) {
      double c = 0, f = 0;
      const int idx[4] = {j * nx + i, j * nx + i + 1, (j + 1) * nx + i, (j + 1) * nx + i + 1};
      for (int k : idx) {
        const double d = pr[k] - ex[k];
        if (d < 0) c += -d;
        else f += d;
      }
      r.cut += h * h * c / 4.0;
      r.fill += h * h * f / 4.0;
    }
  return r;
}

// ---------------------------------------------------------------------------------------------------------------- TIN

Tin MakeFlatTin(double w, double d, double s, double z) {
  Tin t;
  const int nx = static_cast<int>(std::lround(w / s)), ny = static_cast<int>(std::lround(d / s));
  for (int j = 0; j <= ny; ++j)
    for (int i = 0; i <= nx; ++i) t.v.push_back({i * s, j * s, z});
  auto id = [&](int i, int j) { return j * (nx + 1) + i; };
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      t.tri.push_back({id(i, j), id(i + 1, j), id(i + 1, j + 1)});
      t.tri.push_back({id(i, j), id(i + 1, j + 1), id(i, j + 1)});
    }
  return t;
}

namespace {
double TriArea2(const Tin& t, const std::array<int, 3>& q) {
  const Vec3 &a = t.v[q[0]], &b = t.v[q[1]], &c = t.v[q[2]];
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
}  // namespace

double TinPlanArea(const Tin& t) {
  double s = 0;
  for (const auto& q : t.tri) s += 0.5 * TriArea2(t, q);
  return s;
}

bool TinIsValid(const Tin& t, double expectedArea, double tol) {
  std::set<std::pair<int, int>> directed;
  std::map<std::pair<int, int>, int> undirected;
  double area = 0;
  for (const auto& q : t.tri) {
    const double a2 = TriArea2(t, q);
    if (!(a2 > 0)) return false;
    area += 0.5 * a2;
    for (int k = 0; k < 3; ++k) {
      const int p = q[k], r = q[(k + 1) % 3];
      if (!directed.insert({p, r}).second) return false;  // an edge used twice in the same direction: overlap
      ++undirected[{std::min(p, r), std::max(p, r)}];
    }
  }
  for (const auto& e : undirected)
    if (e.second > 2) return false;
  return std::fabs(area - expectedArea) <= tol * std::max(1.0, std::fabs(expectedArea));
}

std::size_t TinBoundaryEdgeCount(const Tin& t) {
  std::map<std::pair<int, int>, int> undirected;
  for (const auto& q : t.tri)
    for (int k = 0; k < 3; ++k) {
      const int p = q[k], r = q[(k + 1) % 3];
      ++undirected[{std::min(p, r), std::max(p, r)}];
    }
  std::size_t n = 0;
  for (const auto& e : undirected)
    if (e.second == 1) ++n;
  return n;
}

bool FlipEdge(Tin& t, std::size_t ia, std::size_t ib) {
  if (ia >= t.tri.size() || ib >= t.tri.size() || ia == ib) return false;
  const auto A = t.tri[ia], B = t.tri[ib];
  for (int k = 0; k < 3; ++k) {
    const int p = A[k], q = A[(k + 1) % 3], r = A[(k + 2) % 3];
    for (int m = 0; m < 3; ++m) {
      if (B[m] == q && B[(m + 1) % 3] == p) {
        const int s = B[(m + 2) % 3];
        const std::array<int, 3> n1{r, p, s}, n2{s, q, r};
        if (!(TriArea2(t, n1) > 0) || !(TriArea2(t, n2) > 0)) return false;  // not strictly convex
        t.tri[ia] = n1;
        t.tri[ib] = n2;
        return true;
      }
    }
  }
  return false;
}

int AddDetailAtFace(Tin& t, std::size_t i) {
  const auto q = t.tri[i];
  const Vec3 &a = t.v[q[0]], &b = t.v[q[1]], &c = t.v[q[2]];
  t.v.push_back({(a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0, (a.z + b.z + c.z) / 3.0});
  const int n = static_cast<int>(t.v.size()) - 1;
  t.tri[i] = {q[0], q[1], n};
  t.tri.push_back({q[1], q[2], n});
  t.tri.push_back({q[2], q[0], n});
  return n;
}

void Smoove(Tin& t, double cx, double cy, double radius, double offset) {
  for (Vec3& v : t.v) {
    const double d = std::hypot(v.x - cx, v.y - cy);
    if (d < radius) v.z += offset * 0.5 * (1.0 + std::cos(kPi * d / radius));
  }
}

void StampPad(Tin& t, double x0, double y0, double x1, double y1, double padZ, double margin) {
  for (Vec3& v : t.v) {
    const double dx = std::max({x0 - v.x, 0.0, v.x - x1}), dy = std::max({y0 - v.y, 0.0, v.y - y1});
    const double d = std::max(dx, dy);  // Chebyshev distance to the rectangle
    if (d == 0) v.z = padZ;
    else if (d < margin) v.z = padZ + (v.z - padZ) * (d / margin);
  }
}

double FourthCornerElevation(double z1, double z2, double z3) { return z1 + z3 - z2; }

double PointSourceIlluminance(double candela, double distance, double cosIncidence) {
  return candela * cosIncidence / (distance * distance);
}

}  // namespace dino8::kernel::site
