// Unit test for PlotStyle / Layer::plot_style_name / LayerPrints /
// EffectivePrintWidthMm / EffectivePlotColor's `style` resolution (doc/
// Document.h) - the actual "no named, reusable plot style table" gap
// PARITY_MAP.md's "Print and plot output" item still names after
// print_width_mm/plot_color (LayerPrintWidth/LayerPlotColor) closed the
// per-layer lineweight/color overrides: a PlotStyle is defined once and
// assigned to any number of layers by name, so changing the one style
// changes every layer that uses it, unlike those two independent
// per-layer fields.
//
// Header-only against doc/Document.h (Layer/PlotStyle/LayerPrints/
// EffectivePrintWidthMm/EffectivePlotColor are all inline) - same
// zero-extra-dependency shape as test_print_width.cpp/test_plot_color.cpp.
#include <cstdio>
#include <string>

#include "doc/Document.h"

using dino8::app::Color;
using dino8::app::EffectivePlotColor;
using dino8::app::EffectivePrintWidthMm;
using dino8::app::Layer;
using dino8::app::LayerPrints;
using dino8::app::PlotStyle;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}
bool SameColor(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
}  // namespace

int main() {
  const Color display = Color::FromBytes(40, 180, 60);  // some on-screen green
  const Color style_color = Color::FromBytes(255, 0, 0);

  // No style at all (nullptr, the pre-existing 2-arg call shape every
  // existing call site keeps using): identical to before this feature
  // existed - test_print_width.cpp/test_plot_color.cpp's own checks are
  // the real regression guard for that; this file only adds the new
  // 3-arg `style` behavior.
  Layer plain;
  Check(LayerPrints(plain, nullptr), "no style: a default layer still prints");
  Check(EffectivePrintWidthMm(plain, 0.25, nullptr) == 0.25, "no style: print width defers to the document default, unchanged");
  Check(SameColor(EffectivePlotColor(plain, display, nullptr), display), "no style: plot color is just the display color, unchanged");

  // A style that sets both a color and a width fully determines both
  // channels, overriding the layer's own (unset) fields.
  PlotStyle full;
  full.has_color = true;
  full.color = style_color;
  full.width_mm = 0.75;
  Check(LayerPrints(plain, &full), "a style with a positive width still prints");
  Check(EffectivePrintWidthMm(plain, 0.25, &full) == 0.75, "the style's own width (0.75) wins over the document default");
  Check(SameColor(EffectivePlotColor(plain, display, &full), style_color), "the style's own color wins over the display color");

  // A style that leaves a channel unset (0 / has_color false) falls back
  // to the layer's own field for THAT channel only - styles and per-layer
  // overrides compose, they don't just replace one another wholesale.
  PlotStyle width_only;
  width_only.width_mm = 1.5;  // has_color stays false
  Layer layer_with_color;
  layer_with_color.has_plot_color = true;
  layer_with_color.plot_color = Color::FromBytes(10, 20, 30);
  Check(EffectivePrintWidthMm(layer_with_color, 0.25, &width_only) == 1.5, "a width-only style still overrides the width");
  Check(SameColor(EffectivePlotColor(layer_with_color, display, &width_only), layer_with_color.plot_color),
        "...but leaves color resolution to the layer's own plot_color (the style sets no color at all)");

  PlotStyle color_only;
  color_only.has_color = true;
  color_only.color = style_color;  // width_mm stays 0
  Layer layer_with_width;
  layer_with_width.print_width_mm = 0.5;
  Check(EffectivePrintWidthMm(layer_with_width, 0.25, &color_only) == 0.5, "a color-only style leaves width resolution to the layer's own print_width_mm");
  Check(SameColor(EffectivePlotColor(layer_with_width, display, &color_only), style_color), "...but still overrides color");

  // A style's negative width means "a layer using this style does not
  // print at all" - the same convention Layer::print_width_mm itself has.
  PlotStyle no_print_style;
  no_print_style.width_mm = -1;
  Check(!LayerPrints(plain, &no_print_style), "a style with a negative width makes an otherwise-printing layer not print");
  Layer explicit_print;
  explicit_print.print_width_mm = 0.5;  // the layer itself says it prints
  Check(!LayerPrints(explicit_print, &no_print_style), "the style's negative width wins over the layer's own positive print_width_mm");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
