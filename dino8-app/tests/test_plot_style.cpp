// Unit test for the named, reusable PlotStyle table (doc/Document.h) - the
// CTB/STB-table half of PARITY_MAP.md's "Print and plot output" item,
// alongside Layer::print_width_mm/has_plot_color/plot_color's own flat
// per-layer-only fields (tests/test_print_width.cpp, test_plot_color.cpp
// already cover those). Exercises ResolvePlotStyle, the 3-argument
// LayerPrints/EffectivePrintWidthMm/EffectivePlotColor overloads
// (io/FileExchange.cpp's ExportSvg/ExportPdf call these, not the flat-only
// 1-/2-argument ones), EffectivePlotAlpha (PlotStyle::transparency, the
// third real CTB/STB column - no flat per-layer field exists for it), plus
// Document::FindPlotStyle/RemovePlotStyle's
// "can't delete what's in use" rule - the same shape
// Document::RemoveAnnotationStyle already has for the current annotation
// style, just checked against every layer's own plot_style field instead of
// one document-wide "current" setting.
#include <cmath>
#include <cstdio>
#include <string>

#include "doc/Document.h"

using dino8::app::Color;
using dino8::app::Document;
using dino8::app::EffectivePlotAlpha;
using dino8::app::EffectivePlotColor;
using dino8::app::EffectivePrintWidthMm;
using dino8::app::Layer;
using dino8::app::LayerPrints;
using dino8::app::PlotStyle;
using dino8::app::ResolvePlotStyle;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}
bool SameColor(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
}  // namespace

int main() {
  const Color display = Color::FromBytes(40, 180, 60);
  const Color style_color = Color::FromBytes(10, 10, 10);

  // --- ResolvePlotStyle / the 3-argument overloads, pure Layer+vector<PlotStyle>.
  {
    std::vector<PlotStyle> styles;
    PlotStyle mono;
    mono.name = "Monochrome";
    mono.has_color = true;
    mono.color = style_color;
    mono.width_mm = 0.5;
    styles.push_back(mono);

    Layer unassigned;
    Check(ResolvePlotStyle(unassigned, styles) == nullptr, "a layer naming no style resolves to nullptr");
    Check(SameColor(EffectivePlotColor(unassigned, styles, display), display), "...and falls back to the display color, same as the flat-field-only overload");
    Check(EffectivePrintWidthMm(unassigned, styles, 0.25) == 0.25, "...and to the document default width");
    Check(LayerPrints(unassigned, styles), "...and prints (no override says otherwise)");

    Layer assigned;
    assigned.plot_style = "Monochrome";
    Check(ResolvePlotStyle(assigned, styles) != nullptr, "a layer naming a real style resolves to that row");
    Check(SameColor(EffectivePlotColor(assigned, styles, display), style_color), "...and the style's own color wins over the display color");
    Check(EffectivePrintWidthMm(assigned, styles, 0.25) == 0.5, "...and the style's own width wins over the document default");
    Check(LayerPrints(assigned, styles), "...and still prints (the style's width is positive, not negative)");

    // A layer's own flat fields are ignored once a named style resolves -
    // the whole point of a *shared* row is that it, not the per-layer
    // copy, decides.
    Layer both;
    both.plot_style = "Monochrome";
    both.has_plot_color = true;
    both.plot_color = Color::FromBytes(255, 0, 0);  // would be red if the flat field won
    both.print_width_mm = 3.0;                      // would be 3mm if the flat field won
    Check(SameColor(EffectivePlotColor(both, styles, display), style_color), "a named style's color overrides the layer's own flat plot_color");
    Check(EffectivePrintWidthMm(both, styles, 0.25) == 0.5, "a named style's width overrides the layer's own flat print_width_mm");

    Layer unknown;
    unknown.plot_style = "NoSuchStyle";
    Check(ResolvePlotStyle(unknown, styles) == nullptr, "a layer naming an unknown/deleted style resolves to nullptr, same as naming none");
    Check(SameColor(EffectivePlotColor(unknown, styles, display), display), "...and falls back to the display color");

    // A non-printing style (width_mm < 0), same three-way convention the
    // flat print_width_mm field already has.
    PlotStyle hidden;
    hidden.name = "NoPrint";
    hidden.width_mm = -1;
    styles.push_back(hidden);
    Layer skip;
    skip.plot_style = "NoPrint";
    Check(!LayerPrints(skip, styles), "a style with a negative width makes its assigned layers not print");

    // --- EffectivePlotAlpha: the third real CTB/STB column, transparency -
    // no flat per-layer field exists to fall back to, so an unassigned or
    // unknown-style layer is simply fully opaque.
    Check(EffectivePlotAlpha(unassigned, styles) == 1.0f, "no style assigned: fully opaque (1.0)");
    Check(EffectivePlotAlpha(unknown, styles) == 1.0f, "an unknown/deleted style name: fully opaque (1.0)");
    Check(EffectivePlotAlpha(assigned, styles) == 1.0f, "Monochrome's own transparency defaults to 0 (fully opaque)");

    PlotStyle fade;
    fade.name = "Fade50";
    fade.transparency = 50;
    styles.push_back(fade);
    Layer faded;
    faded.plot_style = "Fade50";
    Check(std::fabs(EffectivePlotAlpha(faded, styles) - 0.5f) < 1e-6f, "Fade50's 50% transparency resolves to alpha 0.5");

    PlotStyle ghost;
    ghost.name = "Ghost100";
    ghost.transparency = 100;
    styles.push_back(ghost);
    Layer ghosted;
    ghosted.plot_style = "Ghost100";
    Check(EffectivePlotAlpha(ghosted, styles) == 0.0f, "100% transparency resolves to alpha 0.0 (fully transparent)");

    PlotStyle over;
    over.name = "Over";
    over.transparency = 150;  // out of the real 0-100 range
    styles.push_back(over);
    Layer overflow;
    overflow.plot_style = "Over";
    Check(EffectivePlotAlpha(overflow, styles) == 0.0f, "a transparency past 100 is clamped, not UB (never a negative alpha)");
  }

  // --- Document::FindPlotStyle / RemovePlotStyle, including the "can't
  // delete a style a layer still names" rule.
  {
    Document doc;
    Check(doc.PlotStyles().empty(), "a fresh document has no plot styles");
    Check(doc.FindPlotStyle("Monochrome") == nullptr, "FindPlotStyle on an empty table returns nullptr");

    PlotStyle mono;
    mono.name = "Monochrome";
    mono.has_color = true;
    mono.color = style_color;
    doc.PlotStyles().push_back(mono);
    Check(doc.FindPlotStyle("Monochrome") != nullptr, "FindPlotStyle finds a row just added");

    Check(doc.RemovePlotStyle("Monochrome"), "an unused style can be removed");
    Check(doc.FindPlotStyle("Monochrome") == nullptr, "...and is really gone afterward");
    Check(!doc.RemovePlotStyle("Monochrome"), "removing it again fails cleanly (not found)");

    doc.PlotStyles().push_back(mono);
    doc.Layers()[static_cast<size_t>(doc.CurrentLayer())].plot_style = "Monochrome";
    Check(!doc.RemovePlotStyle("Monochrome"), "a style still named by a layer's own plot_style field cannot be removed");
    Check(doc.FindPlotStyle("Monochrome") != nullptr, "...and the row is still there, untouched");

    doc.Layers()[static_cast<size_t>(doc.CurrentLayer())].plot_style.clear();
    Check(doc.RemovePlotStyle("Monochrome"), "once no layer names it any more, it can be removed");
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
