#pragma once

// Outscape (the real-time renderer built into Dino 8) pure maths: exposure, tone curves, sun and sky, fog, lens and
// camera optics, post effects, photometry (IES files) and glass. Header-only and free of the document model, like render_math.h, so the GL rasteriser, the path tracer and
// the tests can share one definition. Every function names the published model or standard it implements; units are
// stated per function (angles in radians unless the name ends in Deg, lengths in the unit the comment says).
// Colours are scene-linear Rec.709 RGB.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "dino8/kernel/render_math.h"

namespace dino8::kernel::outscape {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

// ---- Exposure --------------------------------------------------------------------------------------------------
// Exposure value at ISO 100 from the camera settings (ISO 2720): EV100 = log2(N^2 / t) - log2(S / 100), with N the
// f-number, t the shutter time in seconds and S the ISO speed.
inline double Ev100(double fNumber, double shutterSeconds, double iso) {
  return std::log2(fNumber * fNumber / shutterSeconds) - std::log2(iso / 100.0);
}
// Saturation-based exposure (ISO 12232 with the usual lens constant): the scene luminance that lands on display white
// is 1.2 * 2^EV100 cd/m2.
inline double SaturationLuminance(double ev100) { return 1.2 * std::exp2(ev100); }
// Multiplier that takes scene luminance (cd/m2) to display-referred linear (1 = white) at that exposure value.
inline double ExposureScale(double ev100) { return 1.0 / SaturationLuminance(ev100); }
// Exposure compensation in stops multiplies the linear image by 2^stops.
inline double ExposureCompensationFactor(double stops) { return std::exp2(stops); }
// Reflected-light meter equation (ISO 2720): N^2 / t = L * S / K, so EV100 = log2(L * 100 / K) for the average scene
// luminance L (cd/m2). K is the meter calibration constant (12.5 is the common value).
inline double Ev100ForAverageLuminance(double averageLuminance, double calibrationK = 12.5) {
  return std::log2(averageLuminance * 100.0 / calibrationK);
}
// Log-average (geometric mean) luminance: exp(mean(ln(delta + L))). delta keeps black pixels finite.
inline double LogAverageLuminance(const std::vector<double>& luminance, double delta = 1e-4) {
  if (luminance.empty()) return 0.0;
  double s = 0.0;
  for (double l : luminance) s += std::log(delta + std::max(l, 0.0));
  return std::exp(s / static_cast<double>(luminance.size()));
}
// Automatic exposure by the "key value" rule: scale the image so its log-average luminance becomes the key (0.18 =
// middle grey).
inline double AutoExposureScale(double logAverageLuminance, double key = 0.18) {
  return logAverageLuminance > 0.0 ? key / logAverageLuminance : 1.0;
}

// ---- Display encoding: IEC 61966-2-1 sRGB, 8-bit codes -----------------------------------------------------------
inline double Srgb8ToLinear(int code) {
  return rendermath::SrgbToLinear(std::min(std::max(code, 0), 255) / 255.0);
}
inline int LinearToSrgb8(double linear) {
  const double e = rendermath::LinearToSrgb(rendermath::Clamp(linear, 0.0, 1.0));
  return static_cast<int>(std::lround(e * 255.0));
}

// ---- Tone curves (scene-linear in, display-linear out) -------------------------------------------------------------
// Reinhard et al. 2002, equation 3: L / (1 + L).
inline double ToneReinhard(double l) { return l <= 0.0 ? 0.0 : l / (1.0 + l); }
// Reinhard et al. 2002, equation 4: L (1 + L / Lw^2) / (1 + L); the luminance Lw maps exactly to 1.
inline double ToneReinhardExtended(double l, double lWhite) {
  return l <= 0.0 ? 0.0 : l * (1.0 + l / (lWhite * lWhite)) / (1.0 + l);
}
// The partial curve of Hable's filmic operator (GDC 2010): ((x(Ax + CB) + DE) / (x(Ax + B) + DF)) - E/F with the
// published constants A=0.15 B=0.50 C=0.10 D=0.20 E=0.02 F=0.30.
inline double ToneHablePartial(double x) {
  const double A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
  return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
// Hable's full operator: curve(bias * x) / curve(whitePoint) (published defaults bias 2.0, white point 11.2).
inline double ToneHable(double x, double exposureBias = 2.0, double whitePoint = 11.2) {
  return x <= 0.0 ? 0.0 : ToneHablePartial(exposureBias * x) / ToneHablePartial(whitePoint);
}
// Rational fit of the ACES reference rendering and output transform (Narkowicz 2015), clamped to [0, 1].
inline double ToneAcesFit(double x) {
  if (x <= 0.0) return 0.0;
  const double v = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
  return rendermath::Clamp(v, 0.0, 1.0);
}
// ---- Sun position ----------------------------------------------------------------------------------------------
struct SunPosition {
  double declinationDeg = 0, equationOfTimeMin = 0, hourAngleDeg = 0;
  double elevationDeg = 0;  // geometric (no refraction), above the horizon
  double azimuthDeg = 0;    // from north, clockwise (east = 90)
};
// Julian day of a Gregorian calendar date at `utHours` hours universal time (Meeus, Astronomical Algorithms ch. 7);
// utHours may lie outside 0..24 to cross midnight.
inline double JulianDay(int year, int month, int day, double utHours) {
  int y = year, m = month;
  if (m <= 2) {
    y -= 1;
    m += 12;
  }
  const int a = y / 100;
  const int b = 2 - a + a / 4;
  return std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + day + b - 1524.5 + utHours / 24.0;
}
// Apparent position of the sun from the low-accuracy series of Meeus (Astronomical Algorithms ch. 25, the form used in
// the NOAA solar calculator): geometric mean longitude and anomaly, equation of the centre, apparent longitude with
// nutation and aberration, true obliquity. clockHours is the local clock time as decimal hours, utcOffsetHours the zone
// offset (east positive, including any daylight saving shift the caller wants), lonDeg east positive.
inline SunPosition ComputeSunPosition(int year, int month, int day, double clockHours, double utcOffsetHours,
                                      double latDeg, double lonDeg) {
  const double jd = JulianDay(year, month, day, clockHours - utcOffsetHours);
  const double t = (jd - 2451545.0) / 36525.0;
  const double l0 = 280.46646 + t * (36000.76983 + 0.0003032 * t);
  const double mAnom = (357.52911 + t * (35999.05029 - 0.0001537 * t)) * kDegToRad;
  const double centre = std::sin(mAnom) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
                        std::sin(2 * mAnom) * (0.019993 - 0.000101 * t) + std::sin(3 * mAnom) * 0.000289;
  const double omega = (125.04 - 1934.136 * t) * kDegToRad;
  const double lambda = (l0 + centre - 0.00569 - 0.00478 * std::sin(omega)) * kDegToRad;
  const double eps0 = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
  const double eps = (eps0 + 0.00256 * std::cos(omega)) * kDegToRad;
  const double alpha = std::atan2(std::cos(eps) * std::sin(lambda), std::cos(lambda));
  const double dec = std::asin(std::sin(eps) * std::sin(lambda));
  const double gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t * t - t * t * t / 38710000.0;
  const double ha = (gmst + lonDeg) * kDegToRad - alpha;
  const double lat = latDeg * kDegToRad;
  const double sinEl = std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(ha);
  SunPosition p;
  p.declinationDeg = dec * kRadToDeg;
  double e = std::fmod(l0 - 0.0057183 - alpha * kRadToDeg, 360.0);  // equation of time in degrees of arc, then minutes
  if (e > 180.0) e -= 360.0;
  if (e < -180.0) e += 360.0;
  p.equationOfTimeMin = 4.0 * e;
  double haDeg = std::fmod(ha * kRadToDeg, 360.0);
  if (haDeg > 180.0) haDeg -= 360.0;
  if (haDeg < -180.0) haDeg += 360.0;
  p.hourAngleDeg = haDeg;
  p.elevationDeg = std::asin(rendermath::Clamp(sinEl, -1.0, 1.0)) * kRadToDeg;
  // azimuth measured from south toward west, then turned to north-based clockwise
  const double aSouth = std::atan2(std::sin(ha), std::cos(ha) * std::sin(lat) - std::tan(dec) * std::cos(lat));
  p.azimuthDeg = std::fmod((aSouth + kPi) * kRadToDeg + 360.0, 360.0);
  return p;
}
// Clock time (decimal hours) of solar noon for a longitude, zone offset and equation of time.
inline double SolarNoonClockHours(double lonDeg, double utcOffsetHours, double eotMinutes) {
  return 12.0 - (4.0 * lonDeg - 60.0 * utcOffsetHours + eotMinutes) / 60.0;
}
// Day length from the sunrise hour angle: cos(H0) = -tan(lat) tan(dec) (no refraction, point sun). 0 for a polar
// night, 24 for a midnight sun.
inline double DayLengthHours(double latDeg, double decDeg) {
  const double c = -std::tan(latDeg * kDegToRad) * std::tan(decDeg * kDegToRad);
  if (c >= 1.0) return 0.0;
  if (c <= -1.0) return 24.0;
  return 2.0 * std::acos(c) * kRadToDeg / 15.0;
}

// ---- Sky -------------------------------------------------------------------------------------------------------
// CIE standard overcast sky (Moon and Spencer 1942, ISO 15469): L(zeta) = Lz (1 + 2 cos zeta) / 3, zeta the zenith
// angle (radians) of the sky element.
inline double OvercastSkyLuminance(double zenithLuminance, double zenithAngle) {
  return zenithLuminance * (1.0 + 2.0 * std::cos(zenithAngle)) / 3.0;
}
// Illuminance on an unobstructed horizontal plane from an axially symmetric sky:
// E = 2 pi * integral_0^{pi/2} L(zeta) cos(zeta) sin(zeta) d zeta, by the composite midpoint rule with `steps` panels.
inline double HorizontalIlluminanceFromSky(const std::function<double(double)>& luminanceOfZenithAngle, int steps = 4096) {
  double s = 0.0;
  const double h = (kPi / 2.0) / steps;
  for (int i = 0; i < steps; ++i) {
    const double z = (i + 0.5) * h;
    s += luminanceOfZenithAngle(z) * std::cos(z) * std::sin(z);
  }
  return 2.0 * kPi * s * h;
}
// ---- Fog and absorption ------------------------------------------------------------------------------------------
// Beer-Lambert: transmittance through a homogeneous medium with extinction coefficient sigma (1/length) over a path
// of length d: T = exp(-sigma d).
inline double BeerLambertTransmittance(double sigma, double distance) { return std::exp(-sigma * std::max(distance, 0.0)); }
// Koschmieder: visibility V is the distance at which contrast falls to 2 %, so sigma = -ln(0.02) / V.
inline double ExtinctionFromVisibility(double visibility) { return -std::log(0.02) / visibility; }
// Optical depth of an exponential height fog sigma(h) = sigma0 exp(-k h) along a straight segment of length `length`
// that rises from height h0 to height h1: integral = sigma0 length (e^{-k h0} - e^{-k h1}) / (k (h1 - h0)).
inline double HeightFogOpticalDepth(double sigma0, double k, double h0, double h1, double length) {
  // (e^{-k h0} - e^{-k h1}) / (k (h1 - h0)) = e^{-k h0} * (-expm1(-x)) / x with x = k (h1 - h0): stable when k is tiny.
  const double x = k * (h1 - h0);
  const double lead = sigma0 * length * std::exp(-k * h0);
  if (std::fabs(x) < 1e-300) return lead;
  return lead * (-std::expm1(-x)) / x;
}
// ---- Refraction ------------------------------------------------------------------------------------------------
// Snell's law: sin(theta_t) = n1 sin(theta_i) / n2. Returns false (total internal reflection) when it exceeds 1.
inline bool SnellSinRefracted(double sinIncident, double n1, double n2, double* sinTransmitted) {
  const double s = n1 * sinIncident / n2;
  if (std::fabs(s) > 1.0) return false;
  *sinTransmitted = s;
  return true;
}
inline double CriticalAngle(double nDense, double nRare) { return std::asin(nRare / nDense); }
inline double BrewsterAngle(double n1, double n2) { return std::atan(n2 / n1); }

// ---- Camera optics ---------------------------------------------------------------------------------------------
// Diameter of the blur circle on the sensor for a thin lens (lengths in one unit, e.g. mm): the lens equation
// 1/f = 1/s + 1/v puts the sensor at v_f = f s_f / (s_f - f) for the focus distance s_f; a point at distance s is
// imaged at v = f s / (s - f); the blur disc is A |v - v_f| / v with aperture diameter A = f / N. Closed form:
// c = f^2 |s - s_f| / (N s (s_f - f)).
inline double ThinLensBlurDiameter(double focal, double fNumber, double focusDistance, double objectDistance) {
  return focal * focal * std::fabs(objectDistance - focusDistance) / (fNumber * objectDistance * (focusDistance - focal));
}
inline double HyperfocalDistance(double focal, double fNumber, double acceptableBlur) {
  return focal * focal / (fNumber * acceptableBlur) + focal;
}
inline double DepthOfFieldNear(double focal, double fNumber, double acceptableBlur, double focusDistance) {
  const double h = HyperfocalDistance(focal, fNumber, acceptableBlur);
  return focusDistance * (h - focal) / (h + focusDistance - 2.0 * focal);
}
// Far limit; infinity when the focus distance reaches the hyperfocal distance.
inline double DepthOfFieldFar(double focal, double fNumber, double acceptableBlur, double focusDistance) {
  const double h = HyperfocalDistance(focal, fNumber, acceptableBlur);
  if (focusDistance >= h) return std::numeric_limits<double>::infinity();
  return focusDistance * (h - focal) / (h - focusDistance);
}
// Angle of view across a sensor dimension: 2 atan(sensor / (2 f)).
inline double AngleOfView(double sensorSize, double focal) { return 2.0 * std::atan(sensorSize / (2.0 * focal)); }
inline double FocalForAngleOfView(double sensorSize, double angleOfView) { return sensorSize / (2.0 * std::tan(angleOfView / 2.0)); }
// Motion blur: a shutter angle of a over a frame of 1/fps seconds exposes for (a / 360) / fps seconds.
inline double ShutterSecondsFromAngle(double shutterAngleDeg, double fps) { return (shutterAngleDeg / 360.0) / fps; }
// Length of the blur streak in pixels for an image-space speed in pixels per second.
inline double MotionBlurLengthPixels(double speedPixelsPerSecond, double shutterSeconds) { return speedPixelsPerSecond * shutterSeconds; }

// ---- Lens and post effects -------------------------------------------------------------------------------------
// Natural vignetting, the cos^4 law: relative illumination at an image radius r (same unit as focalLengthInImageUnits)
// is cos^4(atan(r / f)) = (f^2 / (f^2 + r^2))^2.
inline double VignetteCos4(double radius, double focalLengthInImageUnits) {
  const double f2 = focalLengthInImageUnits * focalLengthInImageUnits;
  const double q = f2 / (f2 + radius * radius);
  return q * q;
}
// Normalised 1-D Gaussian kernel of 2 * radius + 1 taps (sums to 1).
inline std::vector<double> GaussianKernel1D(double sigma, int radius) {
  std::vector<double> k(2 * radius + 1);
  double sum = 0;
  for (int i = -radius; i <= radius; ++i) {
    k[i + radius] = std::exp(-0.5 * i * i / (sigma * sigma));
    sum += k[i + radius];
  }
  for (double& v : k) v /= sum;
  return k;
}
// Index of sample i of an n-sample line after mirroring at both ends (the edge sample is repeated, period 2n):
// ... 1 0 | 0 1 2 .. n-1 | n-1 n-2 ...  Works for any i, also when the kernel is wider than the line.
inline int ReflectIndex(int i, int n) {
  const int period = 2 * n;
  int m = i % period;
  if (m < 0) m += period;
  return m < n ? m : period - 1 - m;
}
// Bloom on a luminance plane (row-major, w x h): bright = max(L - threshold, 0), blurred by a separable Gaussian with
// mirrored edges, added back scaled by `intensity`. A symmetric kernel with this edge rule is a doubly stochastic
// operator (each output row and each input column sums to 1), so the energy added is exactly
// intensity * sum(bright), wherever the bright pixels lie, also in the corners and for a kernel wider than the plane.
inline void BloomAdd(std::vector<double>& plane, int w, int h, double threshold, double sigma, int radius, double intensity) {
  std::vector<double> bright(plane.size()), tmp(plane.size(), 0.0), blur(plane.size(), 0.0);
  for (size_t i = 0; i < plane.size(); ++i) bright[i] = std::max(plane[i] - threshold, 0.0);
  const std::vector<double> k = GaussianKernel1D(sigma, radius);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      double s = 0;
      for (int t = -radius; t <= radius; ++t) s += k[t + radius] * bright[static_cast<size_t>(y) * w + ReflectIndex(x + t, w)];
      tmp[static_cast<size_t>(y) * w + x] = s;
    }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      double s = 0;
      for (int t = -radius; t <= radius; ++t) s += k[t + radius] * tmp[static_cast<size_t>(ReflectIndex(y + t, h)) * w + x];
      blur[static_cast<size_t>(y) * w + x] = s;
    }
  for (size_t i = 0; i < plane.size(); ++i) plane[i] += intensity * blur[i];
}

// ---- Photometry --------------------------------------------------------------------------------------------------
// Illuminance (lux) at a surface from a point source of luminous intensity I (cd): E = I max(cos, 0) / d^2.
inline double IlluminanceFromPoint(double candela, double distance, double cosIncidence) {
  return candela * std::max(cosIncidence, 0.0) / (distance * distance);
}
// An isotropic point source of flux Phi (lm) has I = Phi / (4 pi) cd.
inline double CandelaFromIsotropicLumens(double lumens) { return lumens / (4.0 * kPi); }
// A Lambertian emitter of luminance L (cd/m2) and area A (m2) radiates Phi = pi L A into the half space.
inline double LambertianEmitterLumens(double luminance, double areaSquareMetres) { return kPi * luminance * areaSquareMetres; }

// IES LM-63 (IESNA, 1991/1995/2002) photometric data: parsed from text, interpolated and integrated.
// Horizontal angle ranges the file may declare (the symmetry of the luminaire):
//   type C (photometricType 1): one angle: rotationally symmetric; 0..90: the same in every quadrant (I(-h) = I(h) and
//   I(180 - h) = I(h)); 0..180: symmetric about the 0-180 plane (I(360 - h) = I(h)); 90..270: symmetric about the 90-270
//   plane (I(180 - h) = I(h)); 0..360: no symmetry.
//   types A and B (2 and 3): one angle: the same at every horizontal angle; 0..90: symmetric about the vertical plane
//   (I(-h) = I(h)); -90..90: no symmetry. Beyond +-90 degrees the table gives no emission. The vertical angles of an A or B
//   table must run -90..90. Both are recalled from the public format, needs owner check; any other range is refused.
struct IesProfile {
  enum class HorizontalLayout { Unsupported, Rotational, Quadrant, Bilateral0To180, Bilateral90To270, Full360, FrontMirrored, FrontSigned };
  bool ok = false;
  std::string error;
  int lampCount = 0;
  double lumensPerLamp = 0;  // -1 means absolute photometry
  double candelaMultiplier = 1;
  int photometricType = 1;  // 1 = C planes, 2 = B planes, 3 = A planes
  int unitsType = 2;        // 1 = feet, 2 = metres
  double width = 0, length = 0, height = 0;
  double ballastFactor = 1, lampFactor = 1, inputWatts = 0;
  std::vector<double> vAngles, hAngles;
  std::vector<double> candela;  // [h][v], already multiplied by the candela multiplier

  // The layout the horizontal angle list declares for the photometric type (Unsupported for any other range).
  HorizontalLayout Layout() const {
    if (hAngles.empty() || vAngles.empty()) return HorizontalLayout::Unsupported;
    const double f = hAngles.front(), l = hAngles.back(), eps = 1e-9;
    auto is = [&](double a, double b) { return std::fabs(f - a) <= eps && std::fabs(l - b) <= eps; };
    if (hAngles.size() == 1) return HorizontalLayout::Rotational;
    if (photometricType == 1) {
      if (is(0, 90)) return HorizontalLayout::Quadrant;
      if (is(0, 180)) return HorizontalLayout::Bilateral0To180;
      if (is(90, 270)) return HorizontalLayout::Bilateral90To270;
      if (is(0, 360)) return HorizontalLayout::Full360;
      return HorizontalLayout::Unsupported;
    }
    if (std::fabs(vAngles.front() + 90.0) > eps || std::fabs(vAngles.back() - 90.0) > eps) return HorizontalLayout::Unsupported;
    if (is(0, 90)) return HorizontalLayout::FrontMirrored;
    if (is(-90, 90)) return HorizontalLayout::FrontSigned;
    return HorizontalLayout::Unsupported;
  }

  // Candela at vertical angle v and horizontal angle h (degrees), bilinear in the table; the horizontal angle is
  // folded by the symmetry the table declares (see Layout), so the answer is defined for every h. Outside the vertical
  // range of the table, and behind a type A/B table, the luminaire emits nothing (0).
  double CandelaAt(double vDeg, double hDeg) const {
    if (!ok || vAngles.empty() || hAngles.empty()) return 0.0;
    if (vDeg < vAngles.front() - 1e-9 || vDeg > vAngles.back() + 1e-9) return 0.0;
    const HorizontalLayout layout = Layout();
    double h = hAngles.front();
    if (layout == HorizontalLayout::FrontMirrored || layout == HorizontalLayout::FrontSigned) {
      h = std::fmod(hDeg + 180.0, 360.0);
      if (h < 0) h += 360.0;
      h -= 180.0;  // [-180, 180)
      if (layout == HorizontalLayout::FrontMirrored) h = std::fabs(h);
      if (h > 90.0 + 1e-9 || h < -90.0 - 1e-9) return 0.0;
    } else if (layout != HorizontalLayout::Rotational) {
      h = std::fmod(hDeg, 360.0);
      if (h < 0) h += 360.0;  // [0, 360)
      if (layout == HorizontalLayout::Quadrant) {
        if (h > 180.0) h = 360.0 - h;
        if (h > 90.0) h = 180.0 - h;
      } else if (layout == HorizontalLayout::Bilateral0To180) {
        if (h > 180.0) h = 360.0 - h;
      } else if (layout == HorizontalLayout::Bilateral90To270) {
        if (h < 90.0) h = 180.0 - h;
        else if (h > 270.0) h = 540.0 - h;
      }
    }
    const size_t nh = hAngles.size();
    h = std::min(std::max(h, hAngles.front()), hAngles.back());
    size_t h1 = 0;
    while (h1 + 1 < nh - 1 && hAngles[h1 + 1] < h) ++h1;
    size_t h2 = std::min(h1 + 1, nh - 1);
    const double th = (h2 == h1 || hAngles[h2] == hAngles[h1]) ? 0.0 : (h - hAngles[h1]) / (hAngles[h2] - hAngles[h1]);
    const size_t nv = vAngles.size();
    size_t v1 = 0;
    while (v1 + 1 < nv - 1 && vAngles[v1 + 1] < vDeg) ++v1;
    size_t v2 = std::min(v1 + 1, nv - 1);
    const double tv = (v2 == v1 || vAngles[v2] == vAngles[v1]) ? 0.0 : (vDeg - vAngles[v1]) / (vAngles[v2] - vAngles[v1]);
    auto at = [&](size_t hi, size_t vi) { return candela[hi * nv + vi]; };
    const double a = at(h1, v1) * (1 - tv) + at(h1, v2) * tv;
    const double b = at(h2, v1) * (1 - tv) + at(h2, v2) * tv;
    return a * (1 - th) + b * th;
  }
  // Total luminous flux (lm) = integral of I dOmega over the sphere, by the midpoint rule on the interpolated table
  // (0.25 degree in v, 1 degree in h). Type C: dOmega = sin(v) dv dh with v the polar angle (0..180) and h 0..360.
  // Types A and B: v is a latitude and h a longitude on the photometric web, dOmega = cos(v) dv dh with h -90..90
  // (recalled convention, needs owner check; not tested against a closed form).
  double TotalLumens() const {
    if (!ok) return 0.0;
    const double dv = 0.25, dh = 1.0;
    double s = 0.0;
    if (photometricType == 1) {
      for (double v = dv / 2; v < 180.0; v += dv)
        for (double h = dh / 2; h < 360.0; h += dh) s += CandelaAt(v, h) * std::sin(v * kDegToRad);
    } else {
      for (double v = -90.0 + dv / 2; v < 90.0; v += dv)
        for (double h = -90.0 + dh / 2; h < 90.0; h += dh) s += CandelaAt(v, h) * std::cos(v * kDegToRad);
    }
    return s * dv * dh * kDegToRad * kDegToRad;
  }
  // Lumen override: scales every candela value so that the integrated flux becomes `targetLumens`. Refused (false, nothing
  // changed) for a profile that is not ok, has no flux to scale, or a negative or non-finite target.
  bool ScaleToLumens(double targetLumens) {
    const double flux = TotalLumens();
    if (!ok || !(flux > 0.0) || !(targetLumens >= 0.0) || !std::isfinite(targetLumens)) return false;
    const double ratio = targetLumens / flux;
    for (double& c : candela) c *= ratio;
    return true;
  }
};

// Parses the text of an IES LM-63 file: the keyword header up to the TILT line, an optional inline tilt block, the ten
// lamp/geometry numbers, the three electrical numbers, the angle lists and the candela table. A malformed file gives
// ok = false and an error text, never a partial profile.
inline IesProfile ParseIesLm63(const std::string& text) {
  IesProfile p;
  std::istringstream in(text);
  std::string line;
  bool sawTilt = false, tiltInclude = false;
  while (std::getline(in, line)) {
    size_t a = 0;
    while (a < line.size() && std::isspace(static_cast<unsigned char>(line[a]))) ++a;
    std::string t = line.substr(a);
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    std::string up;
    for (char c : t) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (up.rfind("TILT", 0) == 0) {
      sawTilt = true;
      tiltInclude = up.find("INCLUDE") != std::string::npos;
      break;
    }
  }
  if (!sawTilt) {
    p.error = "no TILT line";
    return p;
  }
  std::vector<double> nums;
  std::string rest((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  for (char& c : rest)
    if (c == ',') c = ' ';
  std::istringstream ns(rest);
  std::string tok;
  while (ns >> tok) {
    char* end = nullptr;
    const double v = std::strtod(tok.c_str(), &end);
    if (end == tok.c_str() || *end != '\0') {
      p.error = "non-numeric token '" + tok + "'";
      return p;
    }
    nums.push_back(v);
  }
  size_t i = 0;
  if (tiltInclude) {
    // lamp-to-luminaire geometry, number of tilt angles, the angles, the multiplying factors
    if (nums.size() < 2) {
      p.error = "truncated tilt block";
      return p;
    }
    const size_t nt = static_cast<size_t>(nums[1]);
    i = 2 + 2 * nt;
  }
  if (nums.size() < i + 13) {
    p.error = "truncated header numbers";
    return p;
  }
  p.lampCount = static_cast<int>(nums[i++]);
  p.lumensPerLamp = nums[i++];
  p.candelaMultiplier = nums[i++];
  const int nv = static_cast<int>(nums[i++]);
  const int nh = static_cast<int>(nums[i++]);
  p.photometricType = static_cast<int>(nums[i++]);
  p.unitsType = static_cast<int>(nums[i++]);
  p.width = nums[i++];
  p.length = nums[i++];
  p.height = nums[i++];
  p.ballastFactor = nums[i++];
  p.lampFactor = nums[i++];
  p.inputWatts = nums[i++];
  if (nv < 1 || nh < 1 || p.photometricType < 1 || p.photometricType > 3) {
    p.error = "bad angle counts or photometric type";
    return p;
  }
  const size_t need = i + static_cast<size_t>(nv) + static_cast<size_t>(nh) + static_cast<size_t>(nv) * nh;
  if (nums.size() < need) {
    p.error = "candela table shorter than the declared angle counts";
    return p;
  }
  p.vAngles.assign(nums.begin() + i, nums.begin() + i + nv);
  i += nv;
  p.hAngles.assign(nums.begin() + i, nums.begin() + i + nh);
  i += nh;
  for (size_t k = 1; k < p.vAngles.size(); ++k)
    if (p.vAngles[k] < p.vAngles[k - 1]) {
      p.error = "vertical angles not ascending";
      return p;
    }
  for (size_t k = 1; k < p.hAngles.size(); ++k)
    if (p.hAngles[k] < p.hAngles[k - 1]) {
      p.error = "horizontal angles not ascending";
      return p;
    }
  if (p.Layout() == IesProfile::HorizontalLayout::Unsupported) {
    p.error = "unsupported horizontal angle range for this photometric type";
    return p;
  }
  p.candela.resize(static_cast<size_t>(nv) * nh);
  for (size_t k = 0; k < p.candela.size(); ++k) p.candela[k] = nums[i + k] * p.candelaMultiplier;
  p.ok = true;
  return p;
}

}  // namespace dino8::kernel::outscape
