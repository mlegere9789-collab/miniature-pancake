// Unit test for Layer::has_plot_color/plot_color / EffectivePlotColor
// (doc/Document.h) - the per-layer plot pen color io/FileExchange.cpp's
// ExportSvg/ExportPdf now stroke with instead of each object's own
// on-screen display color, when a layer has one set (LayerPlotColor,
// cmd_layer.cpp). The color half of PARITY_MAP.md's "Print and plot
// output" named "plot styles (CTB/STB)" gap - print_width_mm/
// EffectivePrintWidthMm (tests/test_print_width.cpp) already closed the
// lineweight half. Mirrors real Rhino's own ON_Layer::PlotColor/
// SetPlotColor (io/File3dm.cpp round-trips it through that real .3dm
// field, not a Dino8-only encoding).
//
// Header-only against doc/Document.h (Layer/EffectivePlotColor are both
// inline) - same zero-extra-dependency shape as test_print_width.cpp.
#include <cstdio>
#include <string>

#include "doc/Document.h"

using dino8::app::Color;
using dino8::app::EffectivePlotColor;
using dino8::app::Layer;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}
bool SameColor(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
}  // namespace

int main() {
  const Color display = Color::FromBytes(40, 180, 60);     // some on-screen green
  const Color plot = Color::FromBytes(0, 0, 0);             // the plot-style override: black

  Layer def;
  Check(!def.has_plot_color, "a freshly constructed Layer has no plot color override");
  Check(SameColor(EffectivePlotColor(def, display), display), "with no override, the effective plot color is just the display color");

  Layer overridden;
  overridden.has_plot_color = true;
  overridden.plot_color = plot;
  Check(SameColor(EffectivePlotColor(overridden, display), plot), "with an override set, the effective plot color is the layer's plot_color, not the display color");
  Check(!SameColor(EffectivePlotColor(overridden, display), display), "...and is genuinely different from the display color in this case");

  // Clearing the override (LayerPlotColor ByLayer/Default/None) is just
  // setting has_plot_color back to false - plot_color's own stale value is
  // irrelevant once that happens, same as print_width_mm's 0 "document
  // default" case not caring what some earlier explicit value had been.
  Layer cleared = overridden;
  cleared.has_plot_color = false;
  Check(SameColor(EffectivePlotColor(cleared, display), display), "clearing the override (has_plot_color = false) falls straight back to the display color, regardless of plot_color's leftover value");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
