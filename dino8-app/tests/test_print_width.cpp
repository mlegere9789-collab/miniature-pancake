// Unit test for Layer::print_width_mm / LayerPrints / EffectivePrintWidthMm
// (doc/Document.h) - the per-layer plot lineweight io/FileExchange.cpp's
// ExportSvg/ExportPdf now stroke with, instead of one document-wide
// DrawingOptions::line_width_mm for every layer. Deliberately the same
// three-way convention as real Rhino's own ON_Layer::PlotWeight (0 =
// document default, >0 = explicit width, <0 = layer does not print) - see
// io/File3dm.cpp's SetPlotWeight/PlotWeight round trip through the real
// .3dm field, not a Dino8-only encoding. See PARITY_MAP.md's "Print and
// plot output" item.
//
// Header-only against doc/Document.h (Layer/LayerPrints/EffectivePrintWidthMm
// are all inline) - no Document/SceneObject instantiated, so this links
// nothing at all beyond the standard library, same spirit as
// dino8_test_aci_palette's zero-dependency standalone tests.
#include <cstdio>
#include <string>

#include "doc/Document.h"

using dino8::app::EffectivePrintWidthMm;
using dino8::app::Layer;
using dino8::app::LayerPrints;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}
}  // namespace

int main() {
  Layer def;
  Check(def.print_width_mm == 0, "a freshly constructed Layer defaults to 0 (document default pen width)");
  Check(LayerPrints(def), "a default layer prints");
  Check(EffectivePrintWidthMm(def, 0.25) == 0.25, "print_width_mm 0 defers to the document default (0.25)");

  Layer explicit_width;
  explicit_width.print_width_mm = 0.5;
  Check(LayerPrints(explicit_width), "a layer with an explicit positive width still prints");
  Check(EffectivePrintWidthMm(explicit_width, 0.25) == 0.5, "an explicit positive print_width_mm overrides the document default");
  Check(EffectivePrintWidthMm(explicit_width, 0.9) == 0.5, "the explicit width is independent of what the document default happens to be");

  Layer no_print;
  no_print.print_width_mm = -1;
  Check(!LayerPrints(no_print), "a negative print_width_mm means the layer does not print");

  Layer also_no_print;
  also_no_print.print_width_mm = -0.001;
  Check(!LayerPrints(also_no_print), "any negative value, not just exactly -1, means does not print");

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
