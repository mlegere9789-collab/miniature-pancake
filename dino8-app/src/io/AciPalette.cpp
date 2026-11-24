#include "io/AciPalette.h"

#include <cmath>
#include <limits>

namespace dino8::app {

std::array<int, 3> AciToRgb(int aci) {
  static const std::array<int, 3> base[] = {
      {0, 0, 0},       {255, 0, 0},     {255, 255, 0},   {0, 255, 0},     {0, 255, 255},
      {0, 0, 255},     {255, 0, 255},   {0, 0, 0},       {128, 128, 128}, {192, 192, 192},
  };
  if (aci >= 0 && aci <= 9) return base[aci];
  if (aci >= 250 && aci <= 255) {
    static const int greys[] = {51, 91, 132, 173, 214, 255};
    const int g = greys[aci - 250];
    return {g, g, g};
  }
  if (aci < 10 || aci > 249) return {0, 0, 0};
  const int h = (aci - 10) / 10;  // 24 hues, 15 degrees apart
  const int j = (aci - 10) % 10;
  static const double levels[] = {1.0, 0.8, 0.6, 0.5, 0.3};
  const double v = levels[j / 2];
  const double s = (j % 2) ? 0.5 : 1.0;
  const double hue = h * 15.0 / 60.0;  // in sextants
  const int sector = static_cast<int>(std::floor(hue)) % 6;
  const double f = hue - std::floor(hue);
  const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  double r, g, b;
  switch (sector) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return {static_cast<int>(std::lround(r * 255)), static_cast<int>(std::lround(g * 255)), static_cast<int>(std::lround(b * 255))};
}

int RgbToAci(int r, int g, int b) {
  // Black (the usual "draw on white" colour) is ACI 7 by convention.
  if (r < 8 && g < 8 && b < 8) return 7;
  int best = 7;
  // `long long`, not `long`: on Windows (MSVC, LLP64) `long` is 32 bits, so
  // the previous `1L << 40` sentinel was undefined behaviour (MSVC C4293)
  // that in practice evaluated to a small number, and every colour farther
  // than that from its nearest palette entry silently fell back to ACI 7.
  // The distances themselves fit comfortably in 32 bits (at most 3 * 255^2),
  // but `long long` keeps the sentinel and the accumulation the same width
  // on every platform.
  long long best_d = std::numeric_limits<long long>::max();
  for (int i = 1; i <= 255; ++i) {
    const std::array<int, 3> c = AciToRgb(i);
    const long long dr = c[0] - r, dg = c[1] - g, db = c[2] - b;
    const long long d = dr * dr + dg * dg + db * db;
    if (d < best_d) { best_d = d; best = i; }
  }
  return best;
}

}  // namespace dino8::app
