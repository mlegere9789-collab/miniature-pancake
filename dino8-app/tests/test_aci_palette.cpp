// Unit test for the AutoCAD Colour Index palette (src/io/AciPalette.cpp):
// the ACI -> RGB table and the nearest-ACI search ExportDxf/ExportDwg use
// for group code 62.
//
// This exists first of all as a Windows regression guard. RgbToAci used to
// start its nearest-entry search from `long best_d = 1L << 40`. On Linux and
// macOS `long` is 64 bits and that is a huge sentinel; on Windows (MSVC,
// LLP64) `long` is 32 bits, the shift is undefined behaviour (MSVC C4293)
// and in practice evaluated to a small number, so every colour farther
// than that from its nearest palette entry was silently exported as ACI 7
// on Windows only. Nothing in CI exercised RgbToAci at all, so the
// table below deliberately includes colours that sit far from any palette
// entry, not just the exact primaries a broken sentinel still gets right.
#include <cstdio>
#include <limits>

#include "io/AciPalette.h"

using dino8::app::AciToRgb;
using dino8::app::RgbToAci;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

long long SquaredDistance(const std::array<int, 3>& c, int r, int g, int b) {
  const long long dr = c[0] - r, dg = c[1] - g, db = c[2] - b;
  return dr * dr + dg * dg + db * db;
}

// Independent reference: the same search RgbToAci performs, written out
// here with a sentinel that cannot be narrow on any platform.
int ReferenceRgbToAci(int r, int g, int b) {
  if (r < 8 && g < 8 && b < 8) return 7;
  int best = 7;
  long long best_d = std::numeric_limits<long long>::max();
  for (int i = 1; i <= 255; ++i) {
    const long long d = SquaredDistance(AciToRgb(i), r, g, b);
    if (d < best_d) { best_d = d; best = i; }
  }
  return best;
}
}  // namespace

int main() {
  char label[160];

  // ---- AciToRgb: the fixed entries of the table -------------------------
  struct RgbCase { int aci; int r, g, b; };
  const RgbCase rgb_cases[] = {
      {0, 0, 0, 0},         {1, 255, 0, 0},       {2, 255, 255, 0},     {3, 0, 255, 0},
      {4, 0, 255, 255},     {5, 0, 0, 255},       {6, 255, 0, 255},     {7, 0, 0, 0},
      {8, 128, 128, 128},   {9, 192, 192, 192},   {10, 255, 0, 0},      {30, 255, 128, 0},
      {250, 51, 51, 51},    {255, 255, 255, 255}, {-1, 0, 0, 0},        {256, 0, 0, 0},
  };
  for (const RgbCase& c : rgb_cases) {
    const std::array<int, 3> got = AciToRgb(c.aci);
    std::snprintf(label, sizeof(label), "AciToRgb(%d) = (%d,%d,%d), expected (%d,%d,%d)", c.aci, got[0], got[1],
                  got[2], c.r, c.g, c.b);
    Check(got[0] == c.r && got[1] == c.g && got[2] == c.b, label);
  }

  // ---- RgbToAci: table-driven ---------------------------------------------
  // `distance` is the squared RGB distance from the input to the palette
  // entry it must map to - documented so it is obvious which rows a
  // too-small sentinel would break (any row with a distance above it).
  struct AciCase { int r, g, b; int aci; long long distance; };
  const AciCase aci_cases[] = {
      // Exact palette colours: the seven standard colours and the greys.
      {255, 0, 0, 1, 0},        {255, 255, 0, 2, 0},      {0, 255, 0, 3, 0},        {0, 255, 255, 4, 0},
      {0, 0, 255, 5, 0},        {255, 0, 255, 6, 0},      {0, 0, 0, 7, 0},          {128, 128, 128, 8, 0},
      {192, 192, 192, 9, 0},    {255, 128, 0, 30, 0},     {64, 0, 128, 196, 0},     {51, 51, 51, 250, 0},
      // White is the top of the grey ramp, not ACI 7 (which this palette
      // draws as black - the "draw on white" convention).
      {255, 255, 255, 255, 0},
      // Near-black beyond the < 8 short-circuit: black is still nearest.
      {16, 16, 16, 7, 768},
      // Slightly off-primary: snaps to the primary.
      {250, 10, 10, 1, 225},
      // Far from every palette entry. These are the rows that fail with a
      // 32-bit `1L << 40` sentinel: they came back as ACI 7 on Windows.
      {200, 100, 50, 32, 2520},
      {30, 60, 90, 159, 242},
      {70, 70, 255, 173, 4649},  // farthest-from-palette colour on a 5-step RGB grid
  };
  for (const AciCase& c : aci_cases) {
    const int got = RgbToAci(c.r, c.g, c.b);
    std::snprintf(label, sizeof(label), "RgbToAci(%d,%d,%d) = %d, expected %d", c.r, c.g, c.b, got, c.aci);
    Check(got == c.aci, label);
    // The table's own documentation must be right too.
    const long long d = SquaredDistance(AciToRgb(c.aci), c.r, c.g, c.b);
    std::snprintf(label, sizeof(label), "  ... at squared distance %lld (table says %lld)", d, c.distance);
    Check(d == c.distance, label);
  }

  // ---- Round trip: every palette entry maps back to its own colour --------
  // (Not necessarily its own index: ACI 0 and 7 are both black, ACI 1 and
  // 10 both pure red, and so on. The colour must be preserved exactly.)
  {
    int bad = 0;
    for (int aci = 1; aci <= 255; ++aci) {
      const std::array<int, 3> rgb = AciToRgb(aci);
      const std::array<int, 3> back = AciToRgb(RgbToAci(rgb[0], rgb[1], rgb[2]));
      if (back != rgb) ++bad;
    }
    std::snprintf(label, sizeof(label), "AciToRgb(RgbToAci(AciToRgb(aci))) == AciToRgb(aci) for ACI 1-255 (%d mismatches)",
                  bad);
    Check(bad == 0, label);
  }

  // ---- Cross-check against the reference search over an RGB grid --------
  {
    int bad = 0, probes = 0;
    long long worst = 0;
    for (int r = 0; r < 256; r += 15) {
      for (int g = 0; g < 256; g += 15) {
        for (int b = 0; b < 256; b += 15) {
          ++probes;
          const int got = RgbToAci(r, g, b);
          if (got != ReferenceRgbToAci(r, g, b)) ++bad;
          const long long d = SquaredDistance(AciToRgb(got), r, g, b);
          if (d > worst) worst = d;
        }
      }
    }
    std::snprintf(label, sizeof(label), "RgbToAci matches the reference search on %d grid colours (%d mismatches)",
                  probes, bad);
    Check(bad == 0, label);
    // The grid has to actually reach colours far from the palette, or the
    // cross-check would pass with a broken sentinel too.
    std::snprintf(label, sizeof(label), "grid reaches a colour %lld from its nearest palette entry (> 3000)", worst);
    Check(worst > 3000, label);
    // Every result is a real, writable ACI.
    int out_of_range = 0;
    for (int r = 0; r < 256; r += 15)
      for (int g = 0; g < 256; g += 15)
        for (int b = 0; b < 256; b += 15) {
          const int a = RgbToAci(r, g, b);
          if (a < 1 || a > 255) ++out_of_range;
        }
    std::snprintf(label, sizeof(label), "every RgbToAci result is in 1..255 (%d out of range)", out_of_range);
    Check(out_of_range == 0, label);
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
