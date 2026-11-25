// Tests of include/dino8/kernel/outscape_calc.h (Outscape, the real-time renderer built into Dino 8): exposure, tone curves, sun and
// sky, fog, lens optics, bloom, photometry and glass. One test per Register row of parity/register/enscape (D8-ENSCAPE-*). Every oracle is
// independent of the code under test: a hand-worked closed form (the working is in the comment of the test), a public standard or paper
// (ISO 2720, IEC 61966-2-1, CIE overcast sky, Reinhard 2002, Hable 2010, Narkowicz 2015, LM-63), astronomical event times, or a second
// published model written again here (NOAA/Spencer sun series against the Meeus series in the header; numeric quadrature against a closed
// form). The vendor behaviour that motivates each row is recalled, not observed: the rows say so; these tests check the maths clause only.
// Each test also carries a decoy: a plausible wrong formula that must NOT pass the same check, so the test can fail.
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "dino8/kernel/outscape_calc.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::outscape;
namespace rm = dino8::kernel::rendermath;

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
bool NearRel(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::fabs(b); }

// D8-ENSCAPE-EXPOSURE (manual exposure from camera settings): EV100 = log2(N^2/t) - log2(ISO/100). Hand values: f/4, 1/16 s, ISO 100 gives
// N^2/t = 16 * 16 = 256 = 2^8, EV 8; ISO 400 is two stops faster, EV100 6; ISO 50 gives 9; f/1, 1 s, ISO 100 is EV 0; a shutter twice as long
// (1/8 s) lowers EV by one (7). f/2.8 at 1/60 s: log2(7.84 * 60) = log2(470.4) = 8.877744. Decoy: dropping the ISO term leaves EV 8 at ISO 400.
D8_EXTRA_TEST(Rw5OutscapeEv100FromCameraSettings) {
  d8test::Check(Near(Ev100(4.0, 1.0 / 16.0, 100.0), 8.0, 1e-12), "EV100: f/4, 1/16 s, ISO 100 is exactly 8 (N^2/t = 256)");
  d8test::Check(Near(Ev100(4.0, 1.0 / 16.0, 400.0), 6.0, 1e-12) && Near(Ev100(4.0, 1.0 / 16.0, 50.0), 9.0, 1e-12),
                "EV100: ISO 400 is two stops lower (6), ISO 50 one stop higher (9)");
  d8test::Check(Near(Ev100(1.0, 1.0, 100.0), 0.0, 1e-12) && Near(Ev100(4.0, 1.0 / 8.0, 100.0), 7.0, 1e-12),
                "EV100: f/1 1 s ISO 100 is 0; doubling the shutter time lowers EV by one stop");
  d8test::Check(Near(Ev100(2.8, 1.0 / 60.0, 100.0), 8.877744249949, 1e-9), "EV100: f/2.8 at 1/60 s is log2(470.4) = 8.877744");
  const double noIso = std::log2(4.0 * 4.0 / (1.0 / 16.0));
  d8test::Check(!Near(noIso, Ev100(4.0, 1.0 / 16.0, 400.0), 0.5), "EV100 decoy: a formula without the ISO term is rejected at ISO 400");
}

// D8-ENSCAPE-EXPOSURE (exposure scale and compensation): display white corresponds to 1.2 * 2^EV100 cd/m2, so the scale is
// 1 / (1.2 * 2^EV100). Hand values: EV 0 gives 1/1.2 = 0.8333333; EV 10 gives 1/1228.8 = 8.138020833e-4; EV 15 gives 1/39321.6 = 2.54313151e-5.
// Luminance 1228.8 at EV 10 maps to 1.0. One stop of compensation is a factor 2 (+1 -> 2, -2 -> 0.25, 0 -> 1); a stop higher EV halves the
// image. Decoy: without the factor 1.2 the scale at EV 10 would be 9.765625e-4, 20 % off.
D8_EXTRA_TEST(Rw5OutscapeExposureScaleAndCompensation) {
  d8test::Check(NearRel(ExposureScale(0.0), 0.8333333333333334, 1e-12) && NearRel(ExposureScale(10.0), 8.138020833333334e-4, 1e-12) &&
                    NearRel(ExposureScale(15.0), 2.5431315104166668e-5, 1e-12),
                "exposure scale: 1/(1.2 2^EV) at EV 0, 10 and 15 matches the hand values");
  d8test::Check(Near(1228.8 * ExposureScale(10.0), 1.0, 1e-12) && Near(SaturationLuminance(10.0), 1228.8, 1e-9),
                "exposure scale: the saturation luminance at EV 10 (1228.8 cd/m2) maps to display white");
  d8test::Check(Near(ExposureCompensationFactor(1.0), 2.0, 1e-15) && Near(ExposureCompensationFactor(-2.0), 0.25, 1e-15) &&
                    Near(ExposureCompensationFactor(0.0), 1.0, 1e-15),
                "exposure compensation: +1 stop doubles, -2 stops quarters, 0 leaves");
  d8test::Check(Near(ExposureScale(11.0) * 2.0, ExposureScale(10.0), 1e-18), "exposure scale: one more EV halves the image");
  d8test::Check(!NearRel(ExposureScale(10.0), 1.0 / 1024.0, 0.05), "exposure scale decoy: the formula without the 1.2 factor is rejected");
}

// D8-ENSCAPE-EXPOSURE (automatic exposure): the meter equation N^2/t = L S / K with K = 12.5 gives EV100 = log2(100 L / 12.5): L = 0.125 -> 0,
// L = 1 -> log2(8) = 3, L = 100 -> log2(800) = 9.643856. The key-value rule scales the log-average luminance to 0.18: {1, 4} has geometric mean
// 2 (delta 0), so the scale is 0.09 and the image becomes {0.09, 0.36}; a constant 0.5 image gets scale 0.36 and lands on 0.18. A black pixel
// with delta 1e-4 stays finite: sqrt(1e-4 * 1.0001) for {0, 1}. Decoy: the arithmetic mean of {1, 4} is 2.5, not 2.
D8_EXTRA_TEST(Rw5OutscapeAutoExposureLogAverage) {
  d8test::Check(Near(Ev100ForAverageLuminance(0.125), 0.0, 1e-12) && Near(Ev100ForAverageLuminance(1.0), 3.0, 1e-12) &&
                    Near(Ev100ForAverageLuminance(100.0), 9.643856189774725, 1e-9),
                "meter equation: EV100 of average luminance 0.125, 1 and 100 cd/m2 is 0, 3 and 9.643856");
  const double avg = LogAverageLuminance({1.0, 4.0}, 0.0);
  d8test::Check(Near(avg, 2.0, 1e-12) && Near(AutoExposureScale(avg), 0.09, 1e-12), "auto exposure: log-average of {1,4} is 2, scale 0.18/2 = 0.09");
  d8test::Check(Near(1.0 * AutoExposureScale(avg), 0.09, 1e-12) && Near(4.0 * AutoExposureScale(avg), 0.36, 1e-12), "auto exposure: the scaled image is {0.09, 0.36}");
  const double flat = LogAverageLuminance({0.5, 0.5, 0.5, 0.5}, 0.0);
  d8test::Check(Near(flat, 0.5, 1e-12) && Near(0.5 * AutoExposureScale(flat), 0.18, 1e-12), "auto exposure: a flat 0.5 image is brought to middle grey 0.18");
  const double withBlack = LogAverageLuminance({0.0, 1.0}, 1e-4);
  d8test::Check(std::isfinite(withBlack) && Near(withBlack, std::sqrt(1e-4 * 1.0001), 1e-12), "auto exposure: a black pixel keeps the average finite (delta 1e-4)");
  d8test::Check(!Near(avg, (1.0 + 4.0) / 2.0, 0.1), "auto exposure decoy: the arithmetic mean 2.5 is rejected");
}

// D8-ENSCAPE-EXPOSURE (display encoding): IEC 61966-2-1. Reference values: sRGB 0.5 is linear 0.2140411405; code 128 is 0.2158605001; the
// linear toe ends at 0.0031308 = 0.040449936 encoded (0.0031308 * 12.92); code 10 is 10/255/12.92 = 0.003035269835; linear 0.18 encodes to
// 0.4613561295 and linear 0.5 to 0.7353569831. Every 8-bit code survives decode then encode, and decoded values strictly increase.
// Decoy: a plain 2.2 power gives 0.2176376 for 0.5, which is not 0.2140411.
D8_EXTRA_TEST(Rw5OutscapeSrgbTransferStandard) {
  d8test::Check(Near(rm::SrgbToLinear(0.5), 0.21404114048223255, 1e-12) && Near(Srgb8ToLinear(128), 0.21586050011389926, 1e-12) &&
                    Near(Srgb8ToLinear(10), 0.003035269835488375, 1e-12) && Near(Srgb8ToLinear(255), 1.0, 1e-15) && Near(Srgb8ToLinear(0), 0.0, 1e-15),
                "sRGB decode: reference values at 0.5, codes 128, 10, 255 and 0");
  d8test::Check(Near(rm::LinearToSrgb(0.0031308), 0.040449936, 1e-9) && Near(rm::LinearToSrgb(0.18), 0.46135612950044164, 1e-12) &&
                    Near(rm::LinearToSrgb(0.5), 0.7353569830524495, 1e-12),
                "sRGB encode: the toe end 0.0031308, middle grey 0.18 and 0.5 match the reference values");
  bool roundtrip = true, increasing = true;
  for (int i = 0; i < 256; ++i) {
    if (LinearToSrgb8(Srgb8ToLinear(i)) != i) roundtrip = false;
    if (i > 0 && !(Srgb8ToLinear(i) > Srgb8ToLinear(i - 1))) increasing = false;
  }
  d8test::Check(roundtrip, "sRGB: all 256 codes survive decode then encode");
  d8test::Check(increasing, "sRGB: decoded values strictly increase over the 256 codes");
  d8test::Check(LinearToSrgb8(-0.5) == 0 && LinearToSrgb8(7.0) == 255, "sRGB: out-of-range linear values clamp to codes 0 and 255");
  d8test::Check(!Near(std::pow(0.5, 2.2), 0.21404114048223255, 1e-3), "sRGB decoy: a pure 2.2 gamma is rejected");
}

// D8-ENSCAPE-EXPOSURE (Reinhard tone curve with white point): L(1 + L/Lw^2)/(1 + L). Hand values: Lw = 2, L = 1 gives 1 * 1.25 / 2 = 0.625;
// at L = Lw the curve is exactly 1 (L (1 + 1/L) / (1 + L) = 1); the simple curve L/(1+L) gives 0.5 at 1 and 0.75 at 3 and stays below 1;
// negative input gives 0; the extended curve is never below the simple one. Decoy: the simple curve at Lw = 2 gives 2/3, not 1.
D8_EXTRA_TEST(Rw5OutscapeToneReinhardWhitePoint) {
  d8test::Check(Near(ToneReinhard(1.0), 0.5, 1e-15) && Near(ToneReinhard(3.0), 0.75, 1e-15) && Near(ToneReinhard(0.0), 0.0, 0) && Near(ToneReinhard(-2.0), 0.0, 0) &&
                    ToneReinhard(1e9) < 1.0,
                "Reinhard: 1 -> 0.5, 3 -> 0.75, 0 and negatives -> 0, huge input stays below 1");
  d8test::Check(Near(ToneReinhardExtended(1.0, 2.0), 0.625, 1e-15), "Reinhard extended: L=1 with white point 2 gives 0.625");
  for (double lw : {2.0, 3.0, 4.0, 16.0}) d8test::Check(Near(ToneReinhardExtended(lw, lw), 1.0, 1e-14), "Reinhard extended: the white point maps to exactly 1");
  bool above = true, rising = true;
  double prev = -1;
  for (double l = 0.05; l <= 4.0; l += 0.05) {
    if (ToneReinhardExtended(l, 4.0) < ToneReinhard(l)) above = false;
    if (ToneReinhardExtended(l, 4.0) <= prev) rising = false;
    prev = ToneReinhardExtended(l, 4.0);
  }
  d8test::Check(above && rising, "Reinhard extended: monotone rising and never below the simple curve up to the white point");
  d8test::Check(!Near(ToneReinhard(2.0), 1.0, 0.1), "Reinhard decoy: the simple curve does not reach 1 at the white point");
}

// D8-ENSCAPE-EXPOSURE (filmic tone curve): Hable's operator with the published constants. Hand values (exact rational arithmetic): partial(1) =
// 0.204/0.71 - 1/15 = 0.2206572770; partial(11.2) = 19.38/24.476 - 1/15 = 0.7251293784; so the output at input 0.5 (bias 2 -> 1) is
// 0.2206572770 / 0.7251293784 = 0.3043005615; at input 1 (partial(2)) it is 0.4929185460; the input 5.6 (bias 2 -> 11.2) maps to exactly 1; 0 maps
// to 0 and the curve rises monotonically. Highlight roll-off: the slope of the output falls steadily toward the white point. Exact slopes (the quotient
// rule on the rational, evaluated in exact fractions, f'(x) = 2 P'(2x) / P(11.2)): 0.4667097014 at 0.5, 0.3048794960 at 1, 0.1592070591 at 2, 0.0975670478
// at 3 and 0.0398537246 at the white input 5.6, which is 0.085 of the slope at 0.5; the test compares a central difference of the code with these numbers.
// The linear-space curve has no toe (its slope is largest at 0); the row claims none. Decoys: skipping the division by the white-point value leaves
// partial(2) = 0.3574; a straight clipped ramp x/5.6 has the same slope everywhere, so it does not roll off.
D8_EXTRA_TEST(Rw5OutscapeToneFilmicHable) {
  d8test::Check(Near(ToneHablePartial(1.0), 0.22065727699530516, 1e-12) && Near(ToneHablePartial(11.2), 0.7251293784387427, 1e-12),
                "filmic: partial curve at 1 and at the white point 11.2 matches the hand-worked rationals");
  d8test::Check(Near(ToneHable(0.5), 0.3043005614672469, 1e-12) && Near(ToneHable(1.0), 0.49291854599116464, 1e-12),
                "filmic: full operator at input 0.5 and 1 matches the hand values");
  d8test::Check(Near(ToneHable(5.6), 1.0, 1e-12) && Near(ToneHable(0.0), 0.0, 1e-15) && Near(ToneHablePartial(0.0), 0.0, 1e-15),
                "filmic: the white input maps to 1 and black to 0");
  bool rising = true;
  double prev = -1;
  for (double x = 0.01; x < 5.6; x += 0.01) {
    const double y = ToneHable(x);
    if (y <= prev) rising = false;
    prev = y;
  }
  d8test::Check(rising, "filmic: strictly rising from 0 to the white point");
  auto slope = [](double x) { const double h = 1e-5; return (ToneHable(x + h) - ToneHable(x - h)) / (2.0 * h); };
  d8test::Check(Near(slope(0.5), 0.4667097014052651, 1e-7) && Near(slope(1.0), 0.30487949595690145, 1e-7) && Near(slope(2.0), 0.1592070590603958, 1e-7) &&
                    Near(slope(3.0), 0.0975670478141343, 1e-7) && Near(slope(5.6 - 2e-5), 0.03985372459580501, 2e-4),
                "filmic: the slope at 0.5, 1, 2, 3 and 5.6 matches the exact rational derivative");
  bool slopeFalls = true;
  double prevSlope = 1e9;
  for (double x = 0.05; x <= 5.5; x += 0.05) {
    const double sl = slope(x);
    if (!(sl < prevSlope) || !(sl > 0.0)) slopeFalls = false;
    prevSlope = sl;
  }
  d8test::Check(slopeFalls, "filmic: the slope is positive and falls at every step from 0.05 to 5.5 (highlight roll-off)");
  d8test::Check(slope(5.5) / slope(0.5) < 0.1 && slope(5.5) / slope(0.5) > 0.0, "filmic: near the white point the slope is under a tenth of the mid-tone slope");
  const double rampLow = std::min(0.5 / 5.6, 1.0), rampHigh = std::min(5.5 / 5.6, 1.0);
  d8test::Check(!((rampHigh - rampLow) / 5.0 < 0.1 * (rampLow / 0.5)), "filmic decoy: a clipped straight ramp keeps its slope and does not roll off");
  d8test::Check(!Near(ToneHablePartial(2.0), ToneHable(1.0), 0.05), "filmic decoy: the partial curve without the white-point division is rejected");
}

// D8-ENSCAPE-EXPOSURE (ACES-style tone curve): the rational fit x(2.51x + 0.03) / (x(2.43x + 0.59) + 0.14). Hand values: x = 1 gives
// 2.54 / 3.16 = 0.8037974684; x = 0.18 gives 0.086724 / 0.324932 = 0.2668989204; x = 0.5 gives 0.6163069544; the raw fit exceeds 1 for large
// input (x = 100 gives 1.0305372) and the output is clamped to 1; negatives and 0 give 0. Decoy: the simple Reinhard curve gives 0.5 at 1.
D8_EXTRA_TEST(Rw5OutscapeToneAcesFit) {
  d8test::Check(Near(ToneAcesFit(1.0), 0.8037974683544302, 1e-12) && Near(ToneAcesFit(0.18), 0.2668989203894968, 1e-12) &&
                    Near(ToneAcesFit(0.5), 0.6163069544364508, 1e-12),
                "ACES fit: values at 1, 0.18 and 0.5 match the hand-worked fractions");
  d8test::Check(Near(ToneAcesFit(100.0), 1.0, 0) && Near(ToneAcesFit(0.0), 0.0, 0) && Near(ToneAcesFit(-3.0), 0.0, 0), "ACES fit: clamps to 1 for the raw 1.0305 at x = 100; 0 for x <= 0");
  bool rising = true;
  double prev = -1;
  for (double x = 0.01; x < 20.0; x += 0.01) {
    const double y = ToneAcesFit(x);
    if (y < prev) rising = false;
    prev = y;
  }
  d8test::Check(rising, "ACES fit: never decreases from 0 to 20");
  d8test::Check(!Near(ToneReinhard(1.0), ToneAcesFit(1.0), 0.1), "ACES decoy: the simple Reinhard curve gives a different value at 1");
}

// D8-ENSCAPE-SKY (sun position, astronomical anchors): independent of any series. (a) At the instants of the 2026 equinoxes and solstices (20 Mar
// 14:46, 21 Jun 08:24, 23 Sep 00:05, 21 Dec 20:50 UT) the declination is 0, +23.44, 0, -23.44 degrees (tolerance 0.05, which covers errors of the
// event time of three hours). (b) Midsummer at 51.5 N: the maximum elevation over the day is 90 - 51.5 + 23.44 = 61.94 degrees, due south
// (azimuth 180). (c) At 80 N the sun stays below the horizon on 21 Dec (maximum -13.44) and above it on 21 Jun (minimum +13.44). (d) At the
// equator on the March equinox it rises due east (azimuth 90, tolerance 1). (e) Symmetry about solar noon on 21 Jun at 40 N: elevations equal and
// azimuths summing to 360. Decoy: a sun that ignores the date (declination fixed at +23.44) would be 23 degrees off in December.
D8_EXTRA_TEST(Rw5OutscapeSunAstronomicalAnchors) {
  d8test::Check(Near(ComputeSunPosition(2026, 3, 20, 14.0 + 46.0 / 60.0, 0, 0, 0).declinationDeg, 0.0, 0.05), "sun: declination 0 at the March equinox instant");
  d8test::Check(Near(ComputeSunPosition(2026, 6, 21, 8.0 + 24.0 / 60.0, 0, 0, 0).declinationDeg, 23.44, 0.05), "sun: declination +23.44 at the June solstice instant");
  d8test::Check(Near(ComputeSunPosition(2026, 9, 23, 5.0 / 60.0, 0, 0, 0).declinationDeg, 0.0, 0.05), "sun: declination 0 at the September equinox instant");
  d8test::Check(Near(ComputeSunPosition(2026, 12, 21, 20.0 + 50.0 / 60.0, 0, 0, 0).declinationDeg, -23.44, 0.05), "sun: declination -23.44 at the December solstice instant");
  double bestEl = -90, bestAz = 0;
  for (int i = 0; i <= 240; ++i) {
    const SunPosition p = ComputeSunPosition(2026, 6, 21, 11.0 + i / 120.0, 0, 51.5, 0.0);
    if (p.elevationDeg > bestEl) {
      bestEl = p.elevationDeg;
      bestAz = p.azimuthDeg;
    }
  }
  d8test::Check(Near(bestEl, 61.94, 0.08) && Near(bestAz, 180.0, 0.5), "sun: midsummer noon at 51.5 N peaks at 61.94 degrees due south");
  double maxDec = -90, minJun = 90;
  for (int i = 0; i < 240; ++i) {
    maxDec = std::max(maxDec, ComputeSunPosition(2026, 12, 21, i / 10.0, 0, 80.0, 0.0).elevationDeg);
    minJun = std::min(minJun, ComputeSunPosition(2026, 6, 21, i / 10.0, 0, 80.0, 0.0).elevationDeg);
  }
  d8test::Check(maxDec < 0.0 && Near(maxDec, -13.44, 0.1) && minJun > 0.0 && Near(minJun, 13.44, 0.1), "sun: 80 N has polar night in December (max -13.44) and midnight sun in June (min +13.44)");
  double lo = 5.0, hi = 7.0;  // bisection for the equatorial equinox sunrise
  for (int i = 0; i < 60; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (ComputeSunPosition(2026, 3, 20, mid, 0, 0.0, 0.0).elevationDeg < 0) lo = mid; else hi = mid;
  }
  const SunPosition rise = ComputeSunPosition(2026, 3, 20, hi, 0, 0.0, 0.0);
  d8test::Check(Near(rise.elevationDeg, 0.0, 1e-6) && Near(rise.azimuthDeg, 90.0, 1.0), "sun: the equator sunrise at the equinox is due east");
  const SunPosition noon = ComputeSunPosition(2026, 6, 21, 12.0, 0, 40.0, 0.0);
  const double noonClock = SolarNoonClockHours(0.0, 0, noon.equationOfTimeMin);
  const SunPosition am = ComputeSunPosition(2026, 6, 21, noonClock - 3.0, 0, 40.0, 0.0);
  const SunPosition pm = ComputeSunPosition(2026, 6, 21, noonClock + 3.0, 0, 40.0, 0.0);
  d8test::Check(Near(am.elevationDeg, pm.elevationDeg, 0.03) && Near(am.azimuthDeg + pm.azimuthDeg, 360.0, 0.1), "sun: elevations match and azimuths mirror about solar noon");
  d8test::Check(!Near(23.44, ComputeSunPosition(2026, 12, 21, 20.0 + 50.0 / 60.0, 0, 0, 0).declinationDeg, 5.0), "sun decoy: a fixed +23.44 declination is rejected for December");
}

// Second published model for the cross-check: the NOAA general solar position equations (Fourier series of Spencer 1971) written again
// here, independent of the orbital series in the header.
struct SpencerSun { double elevation, azimuth; };
SpencerSun SpencerSunPosition(int y, int m, int d, double hour, double tz, double lat, double lon) {
  static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  const int doy = cum[m - 1] + d + ((leap && m > 2) ? 1 : 0);
  const double pi = 3.14159265358979323846;
  const double g = 2.0 * pi / (leap ? 366.0 : 365.0) * (doy - 1 + (hour - 12.0) / 24.0);
  const double eot = 229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) - 0.014615 * std::cos(2 * g) - 0.040849 * std::sin(2 * g));
  const double dec = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) - 0.006758 * std::cos(2 * g) + 0.000907 * std::sin(2 * g) -
                     0.002697 * std::cos(3 * g) + 0.00148 * std::sin(3 * g);
  const double tst = hour * 60.0 + eot + 4.0 * lon - 60.0 * tz;
  const double ha = (tst / 4.0 - 180.0) * pi / 180.0;
  const double la = lat * pi / 180.0;
  SpencerSun s;
  s.elevation = std::asin(std::sin(la) * std::sin(dec) + std::cos(la) * std::cos(dec) * std::cos(ha)) * 180.0 / pi;
  s.azimuth = std::fmod((std::atan2(std::sin(ha), std::cos(ha) * std::sin(la) - std::tan(dec) * std::cos(la)) + pi) * 180.0 / pi + 360.0, 360.0);
  return s;
}

// D8-ENSCAPE-SKY (sun position against a second published model): the header uses the Meeus orbital series; this test evaluates the NOAA/Spencer
// Fourier series (a different published model) at eight places, dates and times with the sun between 8 and 70 degrees. The Spencer fit
// is only good to about half a degree around the equinoxes (its declination is a mean-year fit), so the agreement asked for is 0.7 degrees in
// elevation and 1.2 degrees in azimuth; the tight checks are the astronomical anchors of the other test. Decoy: swapping east and west longitude
// moves the sun by tens of degrees and must fail the same comparison.
D8_EXTRA_TEST(Rw5OutscapeSunAgainstSecondModel) {
  struct C { int y, m, d; double h, tz, lat, lon; };
  const C cases[] = {{2026, 3, 20, 9.0, 1, 48.14, 11.58}, {2026, 6, 21, 14.5, -4, 40.71, -74.0},  {2026, 12, 21, 10.0, 0, 51.5, -0.12},
                     {2026, 9, 22, 16.25, 10, -33.87, 151.2}, {2026, 2, 11, 12.0, 0, 30.0, 0.0},   {2026, 7, 4, 8.5, 2, 52.52, 13.4},
                     {2025, 11, 3, 13.0, -8, 37.77, -122.42}, {2027, 5, 14, 17.0, 5.5, 19.07, 72.88}};
  bool allClose = true, decoyFar = true;
  for (const C& c : cases) {
    const SunPosition a = ComputeSunPosition(c.y, c.m, c.d, c.h, c.tz, c.lat, c.lon);
    const SpencerSun b = SpencerSunPosition(c.y, c.m, c.d, c.h, c.tz, c.lat, c.lon);
    double daz = std::fabs(a.azimuthDeg - b.azimuth);
    if (daz > 180.0) daz = 360.0 - daz;
    if (!(a.elevationDeg > 8.0 && a.elevationDeg < 70.0)) allClose = false;
    if (!(std::fabs(a.elevationDeg - b.elevation) <= 0.7 && daz <= 1.2)) allClose = false;
    const SpencerSun wrong = SpencerSunPosition(c.y, c.m, c.d, c.h, c.tz, c.lat, -c.lon);
    if (std::fabs(a.elevationDeg - wrong.elevation) < 1.0 && c.lon != 0.0 && std::fabs(c.lon) > 5.0) decoyFar = false;
  }
  d8test::Check(allClose, "sun: Meeus series and NOAA/Spencer series agree (0.7 degrees elevation, 1.2 azimuth) at eight places and dates");
  d8test::Check(decoyFar, "sun decoy: a mirrored longitude does not agree with the second model");
}

// D8-ENSCAPE-SKY (equation of time and solar noon): almanac values for the equation of time (apparent minus mean solar time) in minutes: about
// -14.2 on 11 Feb (the year's minimum), +3.7 on 14 May, -6.5 on 26 Jul, +16.4 on 3 Nov (the maximum); it crosses zero near 15 Apr, 13 Jun, 1 Sep and
// 25 Dec. Solar noon on the clock is 12:00 - (4 lon - 60 tz + EoT)/60 hours; hand values: lon 0, tz 0, EoT -14.2 -> 12.2367 (12:14); lon 10 E in
// the +1 zone with EoT 0 -> 12 - (40 - 60)/60 = 12.3333 (12:20); lon 15 E in +1 with EoT 0 -> 12.0. Decoy: a table that ignored the date (EoT 0 on 11 Feb).
D8_EXTRA_TEST(Rw5OutscapeEquationOfTimeAndSolarNoon) {
  d8test::Check(Near(ComputeSunPosition(2026, 2, 11, 12.0, 0, 0, 0).equationOfTimeMin, -14.2, 0.4) && Near(ComputeSunPosition(2026, 5, 14, 12.0, 0, 0, 0).equationOfTimeMin, 3.7, 0.4) &&
                    Near(ComputeSunPosition(2026, 7, 26, 12.0, 0, 0, 0).equationOfTimeMin, -6.5, 0.4) && Near(ComputeSunPosition(2026, 11, 3, 12.0, 0, 0, 0).equationOfTimeMin, 16.4, 0.4),
                "equation of time: almanac extremes and mid values within 0.4 minute");
  d8test::Check(Near(ComputeSunPosition(2026, 4, 15, 12.0, 0, 0, 0).equationOfTimeMin, 0.0, 0.7) && Near(ComputeSunPosition(2026, 6, 13, 12.0, 0, 0, 0).equationOfTimeMin, 0.0, 0.7) &&
                    Near(ComputeSunPosition(2026, 9, 1, 12.0, 0, 0, 0).equationOfTimeMin, 0.0, 0.7) && Near(ComputeSunPosition(2026, 12, 25, 12.0, 0, 0, 0).equationOfTimeMin, 0.0, 0.7),
                "equation of time: near zero on the four crossing dates");
  d8test::Check(Near(SolarNoonClockHours(0.0, 0, -14.2), 12.0 + 14.2 / 60.0, 1e-12) && Near(SolarNoonClockHours(10.0, 1, 0.0), 12.0 + 20.0 / 60.0, 1e-12) &&
                    Near(SolarNoonClockHours(15.0, 1, 0.0), 12.0, 1e-12),
                "solar noon: clock time from longitude, zone and equation of time (hand values)");
  d8test::Check(!Near(ComputeSunPosition(2026, 2, 11, 12.0, 0, 0, 0).equationOfTimeMin, 0.0, 5.0), "equation of time decoy: a date-blind zero is rejected for 11 February");
}

// D8-ENSCAPE-SKY (day length from the sunrise hour angle): cos H0 = -tan(lat) tan(dec); day = 2 H0 / 15 hours. Hand values: the equator is 12 h on any
// declination; 45 N at declination 0 is 12 h; 60 N at +23.44 is 2 acos(-0.7510...) / 15 = 18.48983 h; 45 N at +23.44 is 15.42589 h; 33.9 S at
// -23.44 (southern midsummer) is 14.25847 h; 89 N in midsummer is 24 h (midnight sun); 80 S at +23.44 is 0 h (polar night). Decoy: a flat 12 h.
D8_EXTRA_TEST(Rw5OutscapeDayLengthFromSunriseHourAngle) {
  d8test::Check(Near(DayLengthHours(0.0, 23.44), 12.0, 1e-9) && Near(DayLengthHours(0.0, -10.0), 12.0, 1e-9) && Near(DayLengthHours(45.0, 0.0), 12.0, 1e-9), "day length: 12 h at the equator and at equinox");
  d8test::Check(Near(DayLengthHours(60.0, 23.44), 18.489829988, 1e-6) && Near(DayLengthHours(45.0, 23.44), 15.425892413, 1e-6) && Near(DayLengthHours(-33.9, -23.44), 14.258469720, 1e-6),
                "day length: 60 N, 45 N and 33.9 S midsummer match the hand values");
  d8test::Check(Near(DayLengthHours(89.0, 23.44), 24.0, 0) && Near(DayLengthHours(-80.0, 23.44), 0.0, 0), "day length: midnight sun is 24 h, polar night 0 h");
  d8test::Check(!Near(12.0, DayLengthHours(60.0, 23.44), 3.0), "day length decoy: a constant 12 h is rejected at 60 N in June");
}

// D8-ENSCAPE-SKY (overcast sky luminance): the CIE standard overcast sky L(z) = Lz (1 + 2 cos z)/3: zenith Lz, horizon Lz/3, 45 degrees
// Lz (1 + sqrt 2)/3 = 0.8047 Lz. Horizontal illuminance E = 2 pi integral L cos z sin z dz = (2 pi Lz / 3)(1/2 + 2/3) = 7 pi Lz / 9 (closed form;
// the code integrates numerically): Lz = 1000 gives 2443.4610. A uniform sky of luminance L gives pi L. Decoy: the uniform sky with the zenith
// luminance (pi * 1000 = 3141.59) is not the overcast answer.
D8_EXTRA_TEST(Rw5OutscapeOvercastSkyIlluminance) {
  d8test::Check(Near(OvercastSkyLuminance(1000.0, 0.0), 1000.0, 1e-9) && Near(OvercastSkyLuminance(1000.0, kPi / 2.0), 1000.0 / 3.0, 1e-9) &&
                    Near(OvercastSkyLuminance(1.0, kPi / 4.0), (1.0 + std::sqrt(2.0)) / 3.0, 1e-12),
                "overcast sky: zenith Lz, horizon Lz/3, 45 degrees Lz (1 + sqrt 2)/3");
  const double e = HorizontalIlluminanceFromSky([](double z) { return OvercastSkyLuminance(1000.0, z); });
  d8test::Check(NearRel(e, 7.0 * kPi * 1000.0 / 9.0, 1e-6) && Near(e, 2443.4609, 1e-3), "overcast sky: horizontal illuminance is 7 pi Lz / 9 = 2443.46 lux for Lz = 1000");
  d8test::Check(NearRel(HorizontalIlluminanceFromSky([](double) { return 250.0; }), kPi * 250.0, 1e-6), "uniform sky: horizontal illuminance is pi L");
  d8test::Check(!NearRel(e, kPi * 1000.0, 0.05), "overcast decoy: a uniform sky of the zenith luminance is rejected");
}

// D8-ENSCAPE-FOG (Beer-Lambert transmittance): T = exp(-sigma d). Hand values: sigma d = ln 2 gives 0.5; sigma = 0.1, d = 10 gives e^-1 = 0.3678794412;
// d = 0 gives 1; a long path gives almost 0; the transmittance of two stretches multiplies (T(d1 + d2) = T(d1) T(d2)); a negative distance counts as 0.
// Decoy: a linear falloff 1 - sigma d would give 0 at sigma d = 1, not 0.3679.
D8_EXTRA_TEST(Rw5OutscapeFogBeerLambert) {
  d8test::Check(Near(BeerLambertTransmittance(std::log(2.0), 1.0), 0.5, 1e-15) && Near(BeerLambertTransmittance(0.1, 10.0), 0.36787944117144233, 1e-15) &&
                    Near(BeerLambertTransmittance(0.3, 0.0), 1.0, 0) && BeerLambertTransmittance(1.0, 100.0) < 1e-40 && Near(BeerLambertTransmittance(0.3, -5.0), 1.0, 0),
                "fog: transmittance at sigma d = ln 2, 1, 0, large and a negative distance");
  d8test::Check(NearRel(BeerLambertTransmittance(0.07, 3.0 + 4.5), BeerLambertTransmittance(0.07, 3.0) * BeerLambertTransmittance(0.07, 4.5), 1e-14), "fog: transmittance of consecutive stretches multiplies");
  d8test::Check(!Near(1.0 - 0.1 * 10.0, BeerLambertTransmittance(0.1, 10.0), 0.1), "fog decoy: a linear falloff is rejected at sigma d = 1");
}

// D8-ENSCAPE-FOG (visibility to extinction): Koschmieder's relation takes the visibility V as the distance where contrast falls to 2 %, so
// sigma = -ln(0.02) / V = 3.912023005 / V (V = 1000 m gives 0.003912023). Then T(V) = 0.02 exactly, T(V/2) = sqrt(0.02) = 0.1414213562, T(2V) = 0.0004,
// and halving V doubles sigma. Decoy: a 5 % threshold (ln 20 = 2.9957) gives sigma = 0.0029957 for V = 1000.
D8_EXTRA_TEST(Rw5OutscapeFogVisibilityToExtinction) {
  d8test::Check(Near(ExtinctionFromVisibility(1000.0), 0.003912023005428146, 1e-15) && Near(ExtinctionFromVisibility(500.0), 2.0 * ExtinctionFromVisibility(1000.0), 1e-15),
                "visibility: sigma = 3.912023 / V, halving V doubles it");
  const double s = ExtinctionFromVisibility(1000.0);
  d8test::Check(Near(BeerLambertTransmittance(s, 1000.0), 0.02, 1e-15) && Near(BeerLambertTransmittance(s, 500.0), 0.1414213562373095, 1e-13) && Near(BeerLambertTransmittance(s, 2000.0), 0.0004, 1e-15),
                "visibility: transmittance is 0.02 at V, 0.1414 at V/2 and 0.0004 at 2V");
  d8test::Check(!Near(s, std::log(20.0) / 1000.0, 1e-4), "visibility decoy: a 5 % contrast threshold is rejected");
}

// D8-ENSCAPE-FOG (exponential height fog): density sigma0 exp(-k h); optical depth of a straight segment of length L between heights h0 and h1 is
// sigma0 L (e^{-k h0} - e^{-k h1}) / (k (h1 - h0)). Oracle: Simpson quadrature of the density along the ray (2000 panels) written here, plus hand values:
// sigma0 0.01, k 0.1, h 0 -> 30, L 50 gives 0.5 (1 - e^-3) / 3 = 0.1583688219; a level ray (sigma0 0.02, k 0.05, h = 10, L = 100) gives 2 e^-0.5 = 1.2130613;
// a descending ray gives the same value as the ascending one; k -> 0 gives the uniform sigma0 L. Decoy: the density at the mean height (Jensen's gap) gives 0.1116.
D8_EXTRA_TEST(Rw5OutscapeHeightFogOpticalDepth) {
  auto simpson = [](double s0, double k, double h0, double h1, double len) {
    const int n = 2000;
    auto f = [&](double s) { return s0 * std::exp(-k * (h0 + (h1 - h0) * s / len)); };
    double acc = f(0) + f(len);
    for (int i = 1; i < n; ++i) acc += f(len * i / n) * (i % 2 ? 4.0 : 2.0);
    return acc * (len / n) / 3.0;
  };
  d8test::Check(NearRel(HeightFogOpticalDepth(0.01, 0.1, 0.0, 30.0, 50.0), 0.15836882193868934, 1e-12), "height fog: ascending ray matches the hand value 0.1583688");
  d8test::Check(NearRel(HeightFogOpticalDepth(0.01, 0.1, 0.0, 30.0, 50.0), simpson(0.01, 0.1, 0.0, 30.0, 50.0), 1e-9), "height fog: closed form equals Simpson quadrature (ascending)");
  d8test::Check(NearRel(HeightFogOpticalDepth(0.02, 0.05, 10.0, 10.0, 100.0), 1.2130613194252668, 1e-12) && NearRel(HeightFogOpticalDepth(0.02, 0.05, 10.0, 10.0, 100.0), simpson(0.02, 0.05, 10.0, 10.0, 100.0), 1e-9),
                "height fog: level ray 2 e^-0.5 = 1.21306 and quadrature agree");
  d8test::Check(NearRel(HeightFogOpticalDepth(0.01, 0.1, 30.0, 0.0, 50.0), HeightFogOpticalDepth(0.01, 0.1, 0.0, 30.0, 50.0), 1e-12), "height fog: a descending ray has the same optical depth as the ascending one");
  d8test::Check(NearRel(HeightFogOpticalDepth(0.01, 1e-12, 0.0, 30.0, 50.0), 0.5, 1e-9), "height fog: k -> 0 gives the uniform sigma0 L");
  d8test::Check(!NearRel(0.01 * 50.0 * std::exp(-0.1 * 15.0), HeightFogOpticalDepth(0.01, 0.1, 0.0, 30.0, 50.0), 0.05), "height fog decoy: density at the mean height is rejected");
}

// D8-ENSCAPE-CAMERA (depth of field blur circle): thin lens. Hand value: f = 50 mm, N = 2, focus 2000 mm, object 4000 mm: c = 2500 / (2 * 1950) * 2000/4000
// = 0.3205128 mm; an object at 1000 mm gives 0.6410256; at infinity the limit is f^2 / (N (sF - f)) = 0.6410256; zero at the focus distance; doubling N halves
// c. Second route written here from the lens equation 1/f = 1/s + 1/v: blur = (f/N) |v - vF| / v with v = f s/(s - f). Decoy: dividing by the focus distance
// instead of the object distance gives 0.641, not 0.3205.
D8_EXTRA_TEST(Rw5OutscapeThinLensBlurCircle) {
  auto lensEq = [](double f, double N, double sF, double s) {
    const double vF = f * sF / (sF - f), v = f * s / (s - f);
    return (f / N) * std::fabs(v - vF) / v;
  };
  d8test::Check(Near(ThinLensBlurDiameter(50.0, 2.0, 2000.0, 4000.0), 0.3205128205128205, 1e-12) && Near(ThinLensBlurDiameter(50.0, 2.0, 2000.0, 1000.0), 0.6410256410256411, 1e-12),
                "blur circle: f 50, N 2, focus 2000 gives 0.3205128 at 4000 and 0.6410256 at 1000 (hand values)");
  d8test::Check(Near(ThinLensBlurDiameter(50.0, 2.0, 2000.0, 1e12), 0.6410256410256411, 1e-6) && Near(ThinLensBlurDiameter(50.0, 2.0, 2000.0, 2000.0), 0.0, 1e-15), "blur circle: infinity limit and zero at focus");
  bool agree = true;
  for (double s : {300.0, 800.0, 1500.0, 2500.0, 6000.0, 50000.0})
    for (double N : {1.4, 2.8, 8.0})
      if (!NearRel(ThinLensBlurDiameter(35.0, N, 1800.0, s) + 1e-300, lensEq(35.0, N, 1800.0, s) + 1e-300, 1e-12)) agree = false;
  d8test::Check(agree, "blur circle: closed form equals the lens-equation route over 18 object distances and apertures");
  d8test::Check(NearRel(ThinLensBlurDiameter(50.0, 4.0, 2000.0, 4000.0), 0.5 * ThinLensBlurDiameter(50.0, 2.0, 2000.0, 4000.0), 1e-12), "blur circle: doubling the f-number halves the blur");
  d8test::Check(!Near(2500.0 / (2.0 * 1950.0) * 2000.0 / 2000.0, ThinLensBlurDiameter(50.0, 2.0, 2000.0, 4000.0), 0.05), "blur circle decoy: dividing by the focus distance is rejected");
}

// D8-ENSCAPE-CAMERA (hyperfocal distance and depth-of-field limits): H = f^2/(N c) + f. Hand values: f 50, N 8, c 0.03 gives H = 10416.667 + 50 = 10466.667 mm; focused
// at H the near limit is H/2 = 5233.333 (H (H - f)/(2H - 2f)) and the far limit is infinite; focused at 5000 mm the limits are 3389.371 and 9527.439. Second
// route: the limits are where the blur circle reaches c, so s = f^2 sF / (f^2 +- N c (sF - f)) (3389.37 and 9527.44 again) and the blur circle at each limit
// equals c. Decoy: the simple H = f^2/(N c) without + f gives 10416.667.
D8_EXTRA_TEST(Rw5OutscapeHyperfocalAndDepthOfField) {
  const double f = 50.0, N = 8.0, c = 0.03;
  const double H = HyperfocalDistance(f, N, c);
  d8test::Check(Near(H, 10466.666666666668, 1e-9), "hyperfocal: f 50, N 8, c 0.03 gives 10466.667 mm");
  d8test::Check(Near(DepthOfFieldNear(f, N, c, H), H / 2.0, 1e-9) && std::isinf(DepthOfFieldFar(f, N, c, H)) && std::isinf(DepthOfFieldFar(f, N, c, 2.0 * H)), "depth of field: focused at H the near limit is H/2 and the far limit infinite");
  const double near5 = DepthOfFieldNear(f, N, c, 5000.0), far5 = DepthOfFieldFar(f, N, c, 5000.0);
  d8test::Check(Near(near5, 3389.3709327548804, 1e-6) && Near(far5, 9527.439024390242, 1e-6), "depth of field: focus 5000 gives 3389.371 to 9527.439 mm");
  const double nearInv = f * f * 5000.0 / (f * f + N * c * (5000.0 - f)), farInv = f * f * 5000.0 / (f * f - N * c * (5000.0 - f));
  d8test::Check(Near(near5, nearInv, 1e-6) && Near(far5, farInv, 1e-6), "depth of field: the limits equal the inverted blur-circle solution");
  d8test::Check(Near(ThinLensBlurDiameter(f, N, 5000.0, near5), c, 1e-9) && Near(ThinLensBlurDiameter(f, N, 5000.0, far5), c, 1e-9), "depth of field: the blur circle at each limit is exactly c");
  d8test::Check(!Near(f * f / (N * c), H, 1.0), "hyperfocal decoy: leaving out the + f term is rejected");
}

// D8-ENSCAPE-CAMERA (angle of view from focal length and sensor): 2 atan(sensor / 2f). Hand values on a 36 mm wide sensor: 18 mm gives 2 atan(1) = 90
// degrees exactly; 36 mm gives 2 atan(0.5) = 53.1301 degrees; 50 mm gives 39.5978; the 24 mm height at 50 mm gives 26.9915. The inverse of 90 degrees on
// 36 mm is 18 mm and the round trip holds. Decoy: 2 atan(sensor / f) gives 126.87 degrees at 18 mm.
D8_EXTRA_TEST(Rw5OutscapeAngleOfViewFromFocalLength) {
  const double deg = 180.0 / 3.14159265358979323846;
  d8test::Check(Near(AngleOfView(36.0, 18.0) * deg, 90.0, 1e-12) && Near(AngleOfView(36.0, 36.0) * deg, 53.13010235415598, 1e-9) && Near(AngleOfView(36.0, 50.0) * deg, 39.597752709049864, 1e-9) &&
                    Near(AngleOfView(24.0, 50.0) * deg, 26.991466561591622, 1e-9),
                "angle of view: 18 mm -> 90, 36 mm -> 53.1301, 50 mm -> 39.5978 and the 24 mm height -> 26.9915 degrees");
  d8test::Check(Near(FocalForAngleOfView(36.0, 3.14159265358979323846 / 2.0), 18.0, 1e-12) && Near(FocalForAngleOfView(36.0, AngleOfView(36.0, 85.0)), 85.0, 1e-9), "angle of view: inverse gives 18 mm for 90 degrees and round-trips 85 mm");
  d8test::Check(!Near(2.0 * std::atan(36.0 / 18.0) * deg, 90.0, 1.0), "angle of view decoy: 2 atan(sensor/f) is rejected");
}

// D8-ENSCAPE-CAMERA (motion blur from shutter angle): exposure time = (angle / 360) / fps. Hand values: 180 degrees at 24 fps is 1/48 s; 360 at 30 fps is
// 1/30 s; 90 at 25 fps is 1/100 s; 0 gives no blur. A streak of speed 480 px/s over 1/48 s is 10 px. Decoy: ignoring the frame rate (180/360 s) gives 0.5 s.
D8_EXTRA_TEST(Rw5OutscapeMotionBlurShutterAngle) {
  d8test::Check(Near(ShutterSecondsFromAngle(180.0, 24.0), 1.0 / 48.0, 1e-15) && Near(ShutterSecondsFromAngle(360.0, 30.0), 1.0 / 30.0, 1e-15) &&
                    Near(ShutterSecondsFromAngle(90.0, 25.0), 0.01, 1e-15) && Near(ShutterSecondsFromAngle(0.0, 24.0), 0.0, 0),
                "shutter angle: 180 deg at 24 fps = 1/48 s, 360 at 30 = 1/30, 90 at 25 = 1/100, 0 = no exposure");
  d8test::Check(Near(MotionBlurLengthPixels(480.0, ShutterSecondsFromAngle(180.0, 24.0)), 10.0, 1e-12), "motion blur: 480 px/s over 1/48 s is a 10 px streak");
  d8test::Check(!Near(ShutterSecondsFromAngle(180.0, 24.0), 180.0 / 360.0, 0.1), "shutter angle decoy: ignoring the frame rate is rejected");
}

// D8-ENSCAPE-LENS (natural vignetting): the cos^4 law, relative illumination cos^4(atan(r/f)). Hand values: r = f is 45 degrees, cos^4 = (sqrt2/2)^4 = 0.25;
// r = 2f is 1/25 = 0.04; r = f/2 is 0.64; r = 0 is 1; strictly falling with radius. Second form written here: cos^4(atan(r/f)). Decoy: the cos^2 law gives 0.5 at 45 degrees.
D8_EXTRA_TEST(Rw5OutscapeVignetteCos4Law) {
  d8test::Check(Near(VignetteCos4(1.0, 1.0), 0.25, 1e-15) && Near(VignetteCos4(2.0, 1.0), 0.04, 1e-15) && Near(VignetteCos4(0.5, 1.0), 0.64, 1e-15) && Near(VignetteCos4(0.0, 1.0), 1.0, 0),
                "vignette: r = f gives 0.25, 2f gives 0.04, f/2 gives 0.64, centre 1");
  bool same = true, falling = true;
  double prev = 2;
  for (double r = 0.0; r <= 5.0; r += 0.1) {
    const double second = std::pow(std::cos(std::atan(r / 1.7)), 4.0);
    if (!Near(VignetteCos4(r, 1.7), second, 1e-12)) same = false;
    if (VignetteCos4(r, 1.7) >= prev) falling = false;
    prev = VignetteCos4(r, 1.7);
  }
  d8test::Check(same && falling, "vignette: equals cos^4(atan(r/f)) and falls strictly with radius");
  d8test::Check(!Near(std::pow(std::cos(std::atan(1.0)), 2.0), VignetteCos4(1.0, 1.0), 0.1), "vignette decoy: the cos^2 law is rejected");
}

// D8-ENSCAPE-LENS (bloom conserves energy): bright = max(L - threshold, 0), blurred by a normalised Gaussian, added with an intensity. On a 41 x 41 plane of
// 0.2 with one pixel of 5.0 and threshold 1.0, the bright part is 4.0 at the centre only, so the total grows by exactly intensity * 4 = 2.0 (intensity 0.5);
// pixels beyond the kernel radius do not change; the centre gains 0.5 * 4 * k0^2 with k0 = 1/(sigma sqrt(2 pi)) = 0.19947 for sigma 2 (continuous Gaussian,
// tolerance 2e-4 relative for the 4-sigma truncation). The kernel sums to 1. Decoy: an unnormalised kernel sums to about 5.01, which would multiply the added energy.
D8_EXTRA_TEST(Rw5OutscapeBloomConservesEnergy) {
  const int n = 41;
  std::vector<double> plane(n * n, 0.2);
  plane[20 * n + 20] = 5.0;
  const std::vector<double> before = plane;
  BloomAdd(plane, n, n, 1.0, 2.0, 8, 0.5);
  double sumBefore = 0, sumAfter = 0;
  for (size_t i = 0; i < plane.size(); ++i) {
    sumBefore += before[i];
    sumAfter += plane[i];
  }
  d8test::Check(Near(sumAfter - sumBefore, 2.0, 1e-10), "bloom: the total grows by exactly intensity * excess = 2.0 (to summation rounding)");
  d8test::Check(plane[2 * n + 2] == before[2 * n + 2] && plane[38 * n + 38] == before[38 * n + 38] && plane[20 * n + 30] == before[20 * n + 30], "bloom: pixels beyond the kernel radius are untouched");
  const double k0 = 1.0 / (2.0 * std::sqrt(2.0 * 3.14159265358979323846));
  d8test::Check(NearRel(plane[20 * n + 20] - before[20 * n + 20], 0.5 * 4.0 * k0 * k0, 2e-4), "bloom: the centre gain matches the continuous Gaussian peak 0.5 * 4 * k0^2");
  d8test::Check(Near(plane[20 * n + 21], plane[20 * n + 19], 1e-15) && Near(plane[21 * n + 20], plane[19 * n + 20], 1e-15) && plane[20 * n + 21] > plane[20 * n + 22], "bloom: symmetric and falling off from the centre");
  const std::vector<double> k = GaussianKernel1D(2.0, 8);
  double ks = 0;
  for (double v : k) ks += v;
  double raw = 0;
  for (int i = -8; i <= 8; ++i) raw += std::exp(-0.5 * i * i / 4.0);
  d8test::Check(Near(ks, 1.0, 1e-14) && raw > 5.0, "bloom: the kernel sums to 1 (the raw weights would sum to 5.01)");
}

// D8-ENSCAPE-LENS (bloom keeps its energy at the image border): the edge rule mirrors the plane, so a hot pixel in a corner, on an edge, or on a plane narrower
// than the kernel still adds exactly intensity * excess. Closed form for the corner pixel: the mirrored kernel gives the corner source the weight k0 + k1 on each
// axis (its mirror image sits one sample outside), so the corner gain is intensity * excess * (k0 + k1)^2 with k_i = exp(-i^2/8)/S, S the sum over |i| <= 8.
// A second implementation written here pads the plane explicitly (a 17-sample margin, repeated reflection for planes narrower than the margin) and convolves with the
// full two-dimensional kernel; every pixel agrees to 1e-12. Decoy: the zero-padded rule gives the corner only (0.5 + k0/2)^2 = 0.36 of its energy, far from exact.
D8_EXTRA_TEST(Rw5OutscapeBloomConservesEnergyAtBorder) {
  const double intensity = 0.5, threshold = 1.0, sigma = 2.0;
  const int radius = 8;
  std::vector<double> kern(2 * radius + 1);
  double ksum = 0;
  for (int i = -radius; i <= radius; ++i) ksum += std::exp(-0.5 * i * i / (sigma * sigma));
  for (int i = -radius; i <= radius; ++i) kern[i + radius] = std::exp(-0.5 * i * i / (sigma * sigma)) / ksum;
  auto reflect = [](int i, int len) {
    while (i < 0 || i >= len) i = i < 0 ? -1 - i : 2 * len - 1 - i;
    return i;
  };
  auto bloomTotals = [&](int w, int h, const std::vector<std::pair<int, int>>& hot, double value, std::vector<double>* out) {
    std::vector<double> plane(static_cast<size_t>(w) * h, 0.2);
    for (const auto& pr : hot) plane[static_cast<size_t>(pr.second) * w + pr.first] = value;
    const std::vector<double> before = plane;
    BloomAdd(plane, w, h, threshold, sigma, radius, intensity);
    double added = 0;
    for (size_t i = 0; i < plane.size(); ++i) added += plane[i] - before[i];
    // second implementation: explicit padding and a full 2-D kernel
    double maxDiff = 0;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        double g = 0;
        for (int dy = -radius; dy <= radius; ++dy)
          for (int dx = -radius; dx <= radius; ++dx) {
            const double b = std::max(before[static_cast<size_t>(reflect(y + dy, h)) * w + reflect(x + dx, w)] - threshold, 0.0);
            g += kern[dy + radius] * kern[dx + radius] * b;
          }
        maxDiff = std::max(maxDiff, std::fabs(plane[static_cast<size_t>(y) * w + x] - (before[static_cast<size_t>(y) * w + x] + intensity * g)));
      }
    if (out) *out = plane;
    return std::make_pair(added, maxDiff);
  };
  std::vector<double> out;
  auto corner = bloomTotals(41, 41, {{0, 0}}, 5.0, &out);
  d8test::Check(Near(corner.first, intensity * 4.0, 1e-10) && corner.second < 1e-12, "bloom border: a corner hot pixel adds exactly intensity * excess = 2.0 and matches the explicit-padding implementation");
  const double k0 = kern[radius], k1 = kern[radius + 1];
  d8test::Check(Near(out[0] - 5.0, intensity * 4.0 * (k0 + k1) * (k0 + k1), 1e-12), "bloom border: the corner pixel's own gain is intensity * excess * (k0 + k1)^2");
  bool allExact = true;
  for (const auto& pr : std::vector<std::pair<int, int>>{{0, 20}, {40, 40}, {20, 0}, {3, 2}, {40, 0}, {1, 39}}) {
    auto r = bloomTotals(41, 41, {pr}, 5.0, nullptr);
    if (!(Near(r.first, 2.0, 1e-10) && r.second < 1e-12)) allExact = false;
  }
  d8test::Check(allExact, "bloom border: edge, near-edge and other corner pixels also add exactly 2.0");
  auto narrow = bloomTotals(5, 7, {{4, 6}}, 5.0, nullptr);
  auto narrow2 = bloomTotals(5, 7, {{0, 0}, {2, 3}}, 3.0, nullptr);
  d8test::Check(Near(narrow.first, 2.0, 1e-10) && narrow.second < 1e-12 && Near(narrow2.first, 2.0 * 0.5 * 2.0, 1e-10) && narrow2.second < 1e-12,
                "bloom border: a plane narrower than the kernel (5 x 7, radius 8) conserves energy for one and for two hot pixels");
  auto three = bloomTotals(41, 41, {{0, 0}, {40, 0}, {10, 40}}, 3.0, nullptr);
  d8test::Check(Near(three.first, 3.0 * 2.0 * intensity, 1e-10) && three.second < 1e-12, "bloom border: three hot pixels of excess 2 add 3 * 2 * 0.5 = 3.0");
  std::vector<double> outFar;
  bloomTotals(41, 41, {{40, 40}}, 5.0, &outFar);
  bool mirrored = true;
  for (int y = 0; y < 41; ++y)
    for (int x = 0; x < 41; ++x)
      if (!Near(out[static_cast<size_t>(y) * 41 + x], outFar[static_cast<size_t>(40 - y) * 41 + (40 - x)], 1e-15)) mirrored = false;
  d8test::Check(mirrored, "bloom border: the result for the (0,0) pixel is the point mirror of the result for the (40,40) pixel");
  double half = 0;
  for (int i = 0; i <= radius; ++i) half += kern[i + radius];
  d8test::Check(!Near(half * half, 1.0, 0.1), "bloom border decoy: zero padding keeps only (0.5 + k0/2)^2 = 0.36 of the corner energy and is rejected");
}

// D8-ENSCAPE-LIGHT (point light illuminance and photometric units): E = I cos(theta) / d^2. Hand values: 1000 cd at 2 m normal gives 250 lux; at 4 m 62.5 lux
// (quartered); at 60 degrees incidence (cos 0.5) 125 lux; a back-facing surface gets 0. An isotropic 800 lm source is 800/(4 pi) = 63.66198 cd, so at 3 m it
// gives 63.66198 / 9 = 7.07355 lux; I * 4 pi returns the flux (the illuminance summed over a sphere). A Lambertian emitter of 100 cd/m2 and 2 m2 radiates
// pi * 200 = 628.3185 lm. Decoy: an inverse-distance law gives 500 lux at 2 m.
D8_EXTRA_TEST(Rw5OutscapePointLightIlluminance) {
  d8test::Check(Near(IlluminanceFromPoint(1000.0, 2.0, 1.0), 250.0, 1e-12) && Near(IlluminanceFromPoint(1000.0, 4.0, 1.0), 62.5, 1e-12) && Near(IlluminanceFromPoint(1000.0, 2.0, 0.5), 125.0, 1e-12) &&
                    Near(IlluminanceFromPoint(1000.0, 2.0, -0.3), 0.0, 0),
                "point light: 250 lux at 2 m, 62.5 at 4 m, 125 at 60 degrees incidence, 0 on the back");
  const double cd = CandelaFromIsotropicLumens(800.0);
  d8test::Check(Near(cd, 63.66197723675814, 1e-12) && Near(IlluminanceFromPoint(cd, 3.0, 1.0), 7.073553026306459, 1e-12) && Near(cd * 4.0 * 3.14159265358979323846, 800.0, 1e-9), "point light: 800 lm isotropic is 63.662 cd, 7.0736 lux at 3 m, flux returns");
  d8test::Check(Near(LambertianEmitterLumens(100.0, 2.0), 628.3185307179587, 1e-9), "emitter: Lambertian 100 cd/m2 over 2 m2 is 628.3185 lm");
  d8test::Check(!Near(1000.0 / 2.0, IlluminanceFromPoint(1000.0, 2.0, 1.0), 10.0), "point light decoy: an inverse-distance law is rejected");
}

// Hand-authored photometric files for the IES tests (LM-63-2002 layout: keywords, TILT, ten lamp numbers, three electrical numbers, angles, candela).
const char* kIesLambert =  // candela 100 cos(v), v = 0..90 step 5, rotationally symmetric (one horizontal plane): lower hemisphere only
    "IESNA:LM-63-2002\n[TEST] HAND-A\n[MANUFAC] fixture authored for tests\nTILT=NONE\n1 -1 1 19 1 1 2 0 0 0\n1 1 100\n"
    "0 5 10 15 20 25 30 35 40 45 50 55 60 65 70 75 80 85 90\n0\n"
    "100 99.6195 98.4808 96.5926 93.9693 90.6308 86.6025 81.9152 76.6044 70.7107 64.2788 57.3576 50 42.2618 34.202 25.8819 17.3648 8.7156 0\n";
const char* kIesUniform =  // 100 cd in every direction: v = 0..180 step 30, one plane
    "IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 7 1 1 2 0.1 0.1 0.1\n1 1 100\n0 30 60 90 120 150 180\n0\n100 100 100 100 100 100 100\n";
const char* kIesBilateral =  // three planes 0, 90, 180 (bilateral symmetry), v = 0, 90, 180; candela multiplier 2
    "IESNA:LM-63-1995\n[TEST] HAND-C\nTILT=NONE\n2 3000 2 3 3 1 1 0.5 0.4 0.3\n0.9 1.1 36.5\n0 90 180\n0 90 180\n"
    "100 200 300\n10 20 30\n1000 2000 3000\n";
const char* kIesQuadrant =  // planes 0 and 90 (quadrant symmetry), v = 0, 90
    "IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 2 2 1 2 0 0 0\n1 1 50\n0 90\n0 90\n100 200\n300 400\n";
const char* kIesTiltInclude =  // an inline tilt block precedes the numbers: geometry 1, two tilt angles 0 and 90, factors 1.0 and 0.5
    "IESNA:LM-63-2002\nTILT=INCLUDE\n1\n2\n0 90\n1.0 0.5\n1 -1 1 2 1 1 2 0 0 0\n1 1 50\n0 90\n0\n10 20\n";

// D8-ENSCAPE-LIGHT (IES photometric file parsing and lookup): the LM-63 numbers in the hand-authored files. File C: 2 lamps, 3000 lm each, multiplier 2,
// 3 vertical and 3 horizontal angles, type C, feet, 0.5 x 0.4 x 0.3, ballast 0.9, lamp factor 1.1, 36.5 W; its candela rows (planes 0, 90, 180) times the
// multiplier give at v = 90: 400, 40, 4000 and the lookup at (90, 270) folds to the 90 plane (bilateral, 360 - 270 = 90) = 40; at (45, 45) bilinear between
// planes 0 and 90 at v = 45 (150 and 15, times 2: 300 and 30) = 165; v outside 0..180 gives 0. File D (quadrant): (0, 135) folds to plane 45: halfway
// between 100 and 300 = 200; (0, 225) and (0, 315) fold to 45 as well. The inline tilt block is skipped. Malformed files give ok = false with an error:
// no TILT line, a non-numeric token, a table shorter than declared, descending angles. Decoy: a parser that forgets the multiplier gives 20 at (90, 270).
D8_EXTRA_TEST(Rw5OutscapeIesParseAndLookup) {
  const IesProfile c = ParseIesLm63(kIesBilateral);
  d8test::Check(c.ok && c.lampCount == 2 && Near(c.lumensPerLamp, 3000.0, 0) && Near(c.candelaMultiplier, 2.0, 0) && c.vAngles.size() == 3 && c.hAngles.size() == 3 && c.photometricType == 1 && c.unitsType == 1 &&
                    Near(c.width, 0.5, 0) && Near(c.length, 0.4, 0) && Near(c.height, 0.3, 0) && Near(c.ballastFactor, 0.9, 0) && Near(c.lampFactor, 1.1, 0) && Near(c.inputWatts, 36.5, 0),
                "IES: the header numbers of the bilateral file are read");
  d8test::Check(Near(c.CandelaAt(90.0, 0.0), 400.0, 1e-12) && Near(c.CandelaAt(90.0, 90.0), 40.0, 1e-12) && Near(c.CandelaAt(90.0, 180.0), 4000.0, 1e-12) && Near(c.CandelaAt(90.0, 270.0), 40.0, 1e-12),
                "IES: table lookups at the planes, with the multiplier and the bilateral fold (270 -> 90)");
  d8test::Check(Near(c.CandelaAt(45.0, 45.0), 165.0, 1e-12) && Near(c.CandelaAt(-5.0, 0.0), 0.0, 0) && Near(c.CandelaAt(185.0, 0.0), 0.0, 0), "IES: bilinear lookup between planes (165) and zero outside the vertical range");
  const IesProfile q = ParseIesLm63(kIesQuadrant);
  d8test::Check(q.ok && Near(q.CandelaAt(0.0, 45.0), 200.0, 1e-12) && Near(q.CandelaAt(0.0, 135.0), 200.0, 1e-12) && Near(q.CandelaAt(0.0, 225.0), 200.0, 1e-12) && Near(q.CandelaAt(0.0, 315.0), 200.0, 1e-12),
                "IES: quadrant symmetry folds 135, 225 and 315 onto 45 (halfway 100..300 = 200)");
  const IesProfile t = ParseIesLm63(kIesTiltInclude);
  d8test::Check(t.ok && t.lampCount == 1 && t.vAngles.size() == 2 && Near(t.CandelaAt(90.0, 33.0), 20.0, 1e-12), "IES: an inline tilt block is skipped and the candela still reads (20 at v = 90)");
  d8test::Check(!ParseIesLm63("IESNA:LM-63-2002\n1 -1 1 2 1 1 2 0 0 0\n").ok && !ParseIesLm63("IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 2 1 1 2 0 0 0\n1 1 50\n0 90\n0\n10 abc\n").ok &&
                    !ParseIesLm63("IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 3 1 1 2 0 0 0\n1 1 50\n0 90 180\n0\n10 20\n").ok && !ParseIesLm63("IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 2 1 1 2 0 0 0\n1 1 50\n90 0\n0\n10 20\n").ok,
                "IES: no TILT line, a bad token, a short table and descending angles are refused");
  d8test::Check(!Near(c.CandelaAt(90.0, 270.0), 20.0, 1.0), "IES decoy: a parser that drops the candela multiplier is rejected");
}

// D8-ENSCAPE-LIGHT (IES luminous flux): flux = integral of I sin(v) dv dh. Hand values: a uniform 100 cd source over the full sphere gives 4 pi * 100 = 1256.637
// lm (tolerance 1e-4 relative); a Lambertian distribution 100 cos(v) over the lower hemisphere gives pi * 100 = 314.1593 lm (the table is piecewise linear
// at 5 degrees, so the quadrature may differ from the continuous cosine by up to 0.3 %); doubling the candela multiplier (file C style) doubles the flux. A
// table that emits only downward has no flux above the horizon. Decoy: integrating I dv dh without the sin(v) solid-angle weight gives
// 100 pi 2 pi = 1973.9 instead of 1256.6.
D8_EXTRA_TEST(Rw5OutscapeIesLuminousFlux) {
  const double pi = 3.14159265358979323846;
  const IesProfile u = ParseIesLm63(kIesUniform);
  d8test::Check(u.ok && NearRel(u.TotalLumens(), 4.0 * pi * 100.0, 1e-4), "IES flux: a uniform 100 cd source emits 4 pi * 100 = 1256.64 lm");
  const IesProfile l = ParseIesLm63(kIesLambert);
  d8test::Check(l.ok && NearRel(l.TotalLumens(), pi * 100.0, 3e-3), "IES flux: a 100 cos(v) lower-hemisphere table emits pi * 100 = 314.16 lm (within 0.3 %)");
  std::string doubled = kIesUniform;
  doubled.replace(doubled.find("1 -1 1 7"), 8, "1 -1 2 7");
  const IesProfile u2 = ParseIesLm63(doubled);
  d8test::Check(u2.ok && NearRel(u2.TotalLumens(), 2.0 * u.TotalLumens(), 1e-12), "IES flux: a candela multiplier of 2 doubles the flux");
  double down = 0, up = 0;
  for (double v = 0.125; v < 180.0; v += 0.25)
    (v < 90.0 ? down : up) += l.CandelaAt(v, 10.0) * std::sin(v * pi / 180.0);
  d8test::Check(up == 0.0 && down > 0.0, "IES flux: a downward-only table has no emission above the horizon");
  const double unweighted = 100.0 * pi * 2.0 * pi;  // the integral of I dv dh without the sin(v) weight
  d8test::Check(!NearRel(unweighted, 4.0 * pi * 100.0, 0.1), "IES flux decoy: integrating without the sin(v) weight is rejected");
  // the clause "from which a lumen override rescales the intensity": the override target is met through the integrated flux. 100 cd uniform scaled to 2000 lm
  // is 2000 / (4 pi) = 159.1549 cd in every direction (closed form), and the profile then integrates to 2000 lm.
  IesProfile scaled = u;
  d8test::Check(scaled.ScaleToLumens(2000.0) && NearRel(scaled.CandelaAt(60.0, 0.0), 2000.0 / (4.0 * pi), 1e-4) && NearRel(scaled.TotalLumens(), 2000.0, 1e-12),
                "IES flux: overriding to 2000 lm gives 2000 / (4 pi) = 159.155 cd and the profile integrates to 2000 lm");
  d8test::Check(!NearRel(100.0 * 2000.0 / 1000.0, 2000.0 / (4.0 * pi), 0.1), "IES flux decoy: scaling by target over 1000 (a per-1000-lumen reading) does not give the integrated-flux result");
}

// More hand-authored LM-63 files, one per horizontal-angle layout of the public format.
const char* kIesBilateral90 =  // type C, horizontal planes 90, 180, 270 (symmetric about the 90-270 plane), v = 0, 90; plane candela 100/110, 200/210, 300/310
    "IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 2 3 1 2 0 0 0\n1 1 100\n0 90\n90 180 270\n100 110\n200 210\n300 310\n";
const char* kIesTriangle90 =  // the same layout with v = 0, 180 and candela 0, 100, 0 on planes 90, 180, 270 (a triangle wave in h of period 180, mean 50)
    "IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 2 3 1 2 0 0 0\n1 1 100\n0 180\n90 180 270\n0 0\n100 100\n0 0\n";
const char* kIesFull360 =  // type C, planes 0, 90, 180, 270, 360 (no symmetry), v = 0, 90; candela 10/15, 20/25, 30/35, 40/45, 10/15
    "IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 2 5 1 2 0 0 0\n1 1 100\n0 90\n0 90 180 270 360\n10 15\n20 25\n30 35\n40 45\n10 15\n";
const char* kIesFrontSigned =  // type B, v = -90, 0, 90 and h = -90, 0, 90; plane candela 10/11/12, 20/21/22, 30/31/32
    "IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 3 3 2 2 0 0 0\n1 1 100\n-90 0 90\n-90 0 90\n10 11 12\n20 21 22\n30 31 32\n";
const char* kIesFrontMirrored =  // type A, v = -90, 0, 90 and h = 0, 90 (symmetric about the vertical plane); plane candela 20/21/22, 30/31/32
    "IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 3 2 3 2 0 0 0\n1 1 100\n-90 0 90\n0 90\n20 21 22\n30 31 32\n";

// D8-ENSCAPE-LIGHT (IES horizontal-angle layouts, the symmetry of the file): the public LM-63 layouts for type C are one plane (rotational), 0..90 (every
// quadrant alike), 0..180 (symmetric about the 0-180 plane), 90..270 (symmetric about the 90-270 plane: I(180 - h) = I(h)) and 0..360 (none); for types A and B
// 0..90 (I(-h) = I(h)) and -90..90, with no emission beyond +-90 (recalled for A and B, needs owner check). Hand values for the 90..270 file (planes 90, 180,
// 270 at v = 0 with 100, 200, 300): h = 0 gives I(180) = 200; 45 gives I(135) = 150; 300 gives I(240) = 266.667; 350 gives I(190) = 211.111; -30 gives I(210)
// = 233.333; 360 gives 200; and (v = 45, h = 0) is 205 (the vertical interpolation between 200 and 210). A clamping reader would give 100 or 300 there. The
// defining property I(h) = I(180 - h) = I(h + 360) holds over a sweep. Flux of the triangle-wave file: v-independent candela 100 cos-like triangle gives
// 2 * 2 pi * 50 = 200 pi = 628.3185 lm, the clamped reading gives half of it. The 0..360 file: (315) lies between 270 (40) and 360 (10) = 25, -45 wraps
// to the same, 450 is 90 (20), 360 is 10. The signed type B file: (h = -45) between 11 and 21 = 16, 45 gives 26, 120 and -100 give 0 (behind), 270 is -90
// (11), (v = -45, h = 45) is 25.5, (v = 100) is 0, and the same table read as type A gives the same numbers. The mirrored type A file: -45 and 45 both give
// 26, -90 gives 31, -135 gives 0. Ranges outside the format (0..270 or 0..45 for type C, 0..180 or -45..45 for type B, a type B vertical range 0..90) are refused.
D8_EXTRA_TEST(Rw5OutscapeIesHorizontalLayouts) {
  const double pi = 3.14159265358979323846;
  const IesProfile b = ParseIesLm63(kIesBilateral90);
  d8test::Check(b.ok && b.Layout() == IesProfile::HorizontalLayout::Bilateral90To270 && Near(b.CandelaAt(0.0, 90.0), 100.0, 1e-12) && Near(b.CandelaAt(0.0, 180.0), 200.0, 1e-12) && Near(b.CandelaAt(0.0, 270.0), 300.0, 1e-12),
                "IES 90..270: the file parses as that layout and the tabulated planes read back");
  d8test::Check(Near(b.CandelaAt(0.0, 0.0), 200.0, 1e-9) && Near(b.CandelaAt(0.0, 45.0), 150.0, 1e-9) && Near(b.CandelaAt(0.0, 300.0), 266.66666666666667, 1e-9) && Near(b.CandelaAt(0.0, 350.0), 211.11111111111111, 1e-9) &&
                    Near(b.CandelaAt(0.0, -30.0), 233.33333333333334, 1e-9) && Near(b.CandelaAt(0.0, 360.0), 200.0, 1e-9) && Near(b.CandelaAt(45.0, 0.0), 205.0, 1e-9),
                "IES 90..270: angles outside the table mirror about the 90-270 plane (200, 150, 266.667, 211.111, 233.333, 200, 205)");
  bool mirror = true;
  for (double h = -400.0; h <= 400.0; h += 7.0)
    if (!Near(b.CandelaAt(0.0, h), b.CandelaAt(0.0, 180.0 - h), 1e-9) || !Near(b.CandelaAt(0.0, h), b.CandelaAt(0.0, h + 360.0), 1e-9)) mirror = false;
  d8test::Check(mirror, "IES 90..270: I(h) = I(180 - h) = I(h + 360) over a sweep from -400 to 400 degrees");
  const IesProfile tri = ParseIesLm63(kIesTriangle90);
  d8test::Check(tri.ok && NearRel(tri.TotalLumens(), 200.0 * pi, 1e-4), "IES 90..270: the triangle-wave file emits 200 pi = 628.32 lm over the whole sphere");
  const double clampedFlux = 100.0 * pi;  // what reading only the tabulated half (clamping outside 90..270) would integrate to: half of 200 pi
  d8test::Check(!NearRel(clampedFlux, tri.TotalLumens(), 0.1), "IES 90..270 decoy: the clamped reading gives half the flux and is rejected");
  const IesProfile f = ParseIesLm63(kIesFull360);
  d8test::Check(f.ok && f.Layout() == IesProfile::HorizontalLayout::Full360 && Near(f.CandelaAt(0.0, 315.0), 25.0, 1e-9) && Near(f.CandelaAt(0.0, -45.0), 25.0, 1e-9) && Near(f.CandelaAt(0.0, 450.0), 20.0, 1e-9) &&
                    Near(f.CandelaAt(0.0, 360.0), 10.0, 1e-9) && Near(f.CandelaAt(0.0, 180.0), 30.0, 1e-9) && Near(f.CandelaAt(90.0, 270.0), 45.0, 1e-9),
                "IES 0..360: no folding, planes interpolate and angles wrap (315 and -45 give 25, 450 gives 20, 360 gives 10)");
  const IesProfile sg = ParseIesLm63(kIesFrontSigned);
  d8test::Check(sg.ok && sg.Layout() == IesProfile::HorizontalLayout::FrontSigned && Near(sg.CandelaAt(0.0, -45.0), 16.0, 1e-9) && Near(sg.CandelaAt(0.0, 45.0), 26.0, 1e-9) && Near(sg.CandelaAt(0.0, -30.0), 17.666666666666668, 1e-9) &&
                    Near(sg.CandelaAt(0.0, 120.0), 0.0, 0) && Near(sg.CandelaAt(0.0, -100.0), 0.0, 0) && Near(sg.CandelaAt(0.0, 270.0), 11.0, 1e-9) && Near(sg.CandelaAt(-45.0, 45.0), 25.5, 1e-9) && Near(sg.CandelaAt(100.0, 0.0), 0.0, 0),
                "IES type B -90..90: signed horizontal angles interpolate, nothing is emitted beyond +-90, 270 is -90");
  std::string asA = kIesFrontSigned;
  asA.replace(asA.find("1 -1 1 3 3 2 2"), 14, "1 -1 1 3 3 3 2");
  const IesProfile sgA = ParseIesLm63(asA);
  d8test::Check(sgA.ok && sgA.photometricType == 3 && Near(sgA.CandelaAt(0.0, -45.0), 16.0, 1e-9) && Near(sgA.CandelaAt(-45.0, 45.0), 25.5, 1e-9), "IES type A -90..90: the same table reads the same numbers");
  const IesProfile fm = ParseIesLm63(kIesFrontMirrored);
  d8test::Check(fm.ok && fm.Layout() == IesProfile::HorizontalLayout::FrontMirrored && Near(fm.CandelaAt(0.0, -45.0), 26.0, 1e-9) && Near(fm.CandelaAt(0.0, 45.0), 26.0, 1e-9) && Near(fm.CandelaAt(0.0, -90.0), 31.0, 1e-9) &&
                    Near(fm.CandelaAt(0.0, -135.0), 0.0, 0) && Near(fm.CandelaAt(0.0, 0.0), 21.0, 1e-9),
                "IES type A 0..90: negative angles mirror (I(-h) = I(h)) and nothing is emitted beyond +-90");
  auto refused = [](const char* hdr, const char* vs, const char* hs, const char* body) {
    std::string t = std::string("IESNA:LM-63-2002\nTILT=NONE\n") + hdr + "\n1 1 100\n" + vs + "\n" + hs + "\n" + body + "\n";
    return !ParseIesLm63(t).ok && ParseIesLm63(t).error.find("unsupported") != std::string::npos;
  };
  d8test::Check(refused("1 -1 1 2 2 1 2 0 0 0", "0 90", "0 270", "1 2\n3 4") && refused("1 -1 1 2 2 1 2 0 0 0", "0 90", "0 45", "1 2\n3 4") && refused("1 -1 1 2 2 1 2 0 0 0", "0 90", "45 90", "1 2\n3 4") &&
                    refused("1 -1 1 3 2 2 2 0 0 0", "-90 0 90", "0 180", "1 2 3\n4 5 6") && refused("1 -1 1 3 2 2 2 0 0 0", "-90 0 90", "-45 45", "1 2 3\n4 5 6") && refused("1 -1 1 2 2 2 2 0 0 0", "0 90", "0 90", "1 2\n3 4"),
                "IES: a horizontal range outside the format (type C 0..270, 0..45, 45..90; type B 0..180, -45..45) and a type B vertical range 0..90 are refused with an error");
  // the two older layouts at angles that are not 45 degrees from a plane (a fold by the wrong constant would pass at 135 and 225)
  const IesProfile qd = ParseIesLm63(kIesQuadrant);
  d8test::Check(qd.ok && qd.Layout() == IesProfile::HorizontalLayout::Quadrant && Near(qd.CandelaAt(0.0, 100.0), 100.0 + 200.0 * 80.0 / 90.0, 1e-9) && Near(qd.CandelaAt(0.0, 260.0), 100.0 + 200.0 * 80.0 / 90.0, 1e-9) &&
                    Near(qd.CandelaAt(0.0, 350.0), 100.0 + 200.0 * 10.0 / 90.0, 1e-9) && Near(qd.CandelaAt(0.0, -100.0), 100.0 + 200.0 * 80.0 / 90.0, 1e-9),
                "IES 0..90: quadrant folds at 100, 260, 350 and -100 degrees (277.78 and 122.22) with 100 on plane 0 and 300 on plane 90");
  const IesProfile bi = ParseIesLm63(kIesBilateral);
  d8test::Check(bi.Layout() == IesProfile::HorizontalLayout::Bilateral0To180 && Near(bi.CandelaAt(90.0, 200.0), 40.0 + 3960.0 * 70.0 / 90.0, 1e-9) && Near(bi.CandelaAt(90.0, 350.0), 400.0 - 360.0 * 10.0 / 90.0, 1e-9),
                "IES 0..180: bilateral folds 200 to 160 (3120) and 350 to 10 (360) with the multiplier applied");
  // decoy: the clamping reader gives the first or last plane for h outside the table
  const double clampAt0 = 100.0;
  d8test::Check(!Near(clampAt0, b.CandelaAt(0.0, 0.0), 50.0), "IES decoy: a reader that clamps to the nearest tabulated plane (100) is rejected at h = 0");
}

// D8-ENSCAPE-LIGHT (lumen override): a lumen value replaces the file's rating and every candela value is scaled by the ratio of the new flux to the integrated
// flux, so the profile then integrates to the new value. Hand values on the uniform 100 cd file (1256.637 lm): overriding to 2513.274 lm doubles every candela
// (200 cd; relative tolerance 1e-5 for the quadrature of the original flux), to 628.3185 lm halves it (50 cd), to the same flux leaves it; to 1000 lm gives
// 1000 / (4 pi) = 79.5775 cd; the integrated flux after each override is the target to 1e-12. The cosine file (314.159 lm) overridden to 628.3185 lm doubles the
// peak candela (200). Refused with no change: a negative target, a not-a-number target, a profile that was not read, a profile whose table is all zero.
// Decoy: scaling by the target over 1000 (a per-1000-lumen reading of the file) would give 2.513 for 2513.274.
D8_EXTRA_TEST(Rw5OutscapeIesLumenOverride) {
  const double pi = 3.14159265358979323846;
  IesProfile u = ParseIesLm63(kIesUniform);
  d8test::Check(u.ok && u.ScaleToLumens(8.0 * pi * 100.0) && NearRel(u.CandelaAt(75.0, 0.0), 200.0, 1e-5) && NearRel(u.TotalLumens(), 8.0 * pi * 100.0, 1e-12), "lumen override: 1256.64 lm -> 2513.27 lm doubles every candela and the flux is the target");
  IesProfile h = ParseIesLm63(kIesUniform);
  d8test::Check(h.ScaleToLumens(2.0 * pi * 100.0) && NearRel(h.CandelaAt(120.0, 0.0), 50.0, 1e-5) && NearRel(h.TotalLumens(), 2.0 * pi * 100.0, 1e-12), "lumen override: 628.32 lm halves every candela");
  IesProfile same = ParseIesLm63(kIesUniform);
  const double flux0 = same.TotalLumens();
  d8test::Check(same.ScaleToLumens(flux0) && NearRel(same.CandelaAt(10.0, 0.0), 100.0, 1e-12), "lumen override: the same flux leaves the candela alone");
  IesProfile k = ParseIesLm63(kIesUniform);
  d8test::Check(k.ScaleToLumens(1000.0) && NearRel(k.CandelaAt(90.0, 0.0), 1000.0 / (4.0 * pi), 1e-5) && NearRel(k.TotalLumens(), 1000.0, 1e-12), "lumen override: 1000 lm is 1000 / (4 pi) = 79.58 cd in every direction");
  IesProfile l = ParseIesLm63(kIesLambert);
  const double peak0 = l.CandelaAt(0.0, 0.0);
  d8test::Check(l.ScaleToLumens(2.0 * pi * 100.0) && NearRel(l.CandelaAt(0.0, 0.0), 2.0 * peak0, 3e-3) && NearRel(l.CandelaAt(60.0, 0.0), 2.0 * 50.0, 3e-3) && NearRel(l.TotalLumens(), 2.0 * pi * 100.0, 1e-12),
                "lumen override: a 100 cos(v) file overridden to twice its flux doubles the peak (200) and the 60 degree value (100)");
  IesProfile bad = ParseIesLm63(kIesUniform);
  const double c0 = bad.CandelaAt(60.0, 0.0);
  IesProfile zero = ParseIesLm63(kIesUniform);
  for (double& c : zero.candela) c = 0.0;
  IesProfile unread = ParseIesLm63("not a photometric file");
  d8test::Check(!bad.ScaleToLumens(-5.0) && !bad.ScaleToLumens(std::nan("")) && !bad.ScaleToLumens(std::numeric_limits<double>::infinity()) && Near(bad.CandelaAt(60.0, 0.0), c0, 0) && !zero.ScaleToLumens(500.0) && !unread.ScaleToLumens(500.0),
                "lumen override: a negative, not-a-number or infinite target, an all-zero table and an unread profile are refused without change");
  d8test::Check(!Near(2.0 * pi * 400.0 / 1000.0, 200.0, 1.0), "lumen override decoy: dividing the target by 1000 does not give the doubled candela");
}

// D8-ENSCAPE-GLASS (glass reflection and refraction): Snell and Fresnel for n = 1.5. Hand values: normal incidence reflects ((1.5 - 1)/(1.5 + 1))^2 = 0.04;
// at Brewster's angle atan(1.5) = 56.30993 degrees the p component vanishes and s reflects sin^2(theta_i - theta_t) with theta_i + theta_t = 90 degrees, i.e.
// sin(22.6199 degrees) = 5/13, so the unpolarised reflectance is (25/169)/2 = 25/338 = 0.07396450; the critical angle from glass to air is asin(1/1.5) =
// 41.81031 degrees; beyond it the reflectance is 1; sin(theta_t) for 30 degrees air to glass is 0.5/1.5 = 0.3333333; grazing incidence reflects 1; the
// reflectance rises with angle. Decoy: swapping the index ratio (glass to air at the Brewster cosine) gives total reflection, not 0.0740.
D8_EXTRA_TEST(Rw5OutscapeGlassFresnelAndSnell) {
  const double pi = 3.14159265358979323846;
  d8test::Check(Near(rm::FresnelDielectricCos(1.0, 1.5), 0.04, 1e-12), "glass: normal-incidence reflectance is 0.04");
  const double brewster = BrewsterAngle(1.0, 1.5);
  d8test::Check(Near(brewster * 180.0 / pi, 56.309932474020215, 1e-9) && Near(rm::FresnelDielectricCos(std::cos(brewster), 1.5), 25.0 / 338.0, 1e-12), "glass: at Brewster's angle (56.3099 degrees) the unpolarised reflectance is 25/338");
  const double crit = CriticalAngle(1.5, 1.0);
  d8test::Check(Near(crit * 180.0 / pi, 41.810314895778596, 1e-9), "glass: critical angle 41.8103 degrees");
  double sinT = 0;
  d8test::Check(SnellSinRefracted(0.5, 1.0, 1.5, &sinT) && Near(sinT, 1.0 / 3.0, 1e-15), "glass: 30 degrees air to glass refracts to sin = 1/3");
  d8test::Check(!SnellSinRefracted(std::sin(42.0 * pi / 180.0), 1.5, 1.0, &sinT) && SnellSinRefracted(std::sin(41.0 * pi / 180.0), 1.5, 1.0, &sinT), "glass: total internal reflection at 42 degrees, transmission at 41 degrees (glass to air)");
  d8test::Check(Near(rm::FresnelDielectricCos(std::cos(42.0 * pi / 180.0), 1.0 / 1.5), 1.0, 0) && rm::FresnelDielectricCos(std::cos(41.0 * pi / 180.0), 1.0 / 1.5) < 1.0, "glass: reflectance is 1 beyond the critical angle");
  bool rising = true;
  double prev = 0;
  for (int a = 0; a <= 89; ++a) {
    const double r = rm::FresnelDielectricCos(std::cos(a * pi / 180.0), 1.5);
    if (r < prev) rising = false;
    prev = r;
  }
  d8test::Check(rising && Near(rm::FresnelDielectricCos(1e-9, 1.5), 1.0, 1e-6), "glass: reflectance rises with the angle and reaches 1 at grazing incidence");
  d8test::Check(!Near(rm::FresnelDielectricCos(std::cos(brewster), 1.0 / 1.5), 25.0 / 338.0, 0.1), "glass decoy: the inverted index ratio is rejected at Brewster's angle");
}

}  // namespace
