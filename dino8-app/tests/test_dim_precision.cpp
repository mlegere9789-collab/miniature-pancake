// Unit test for AnnotationStyle::linear_precision (Document.h) and the
// FormatDimensionNumber/precision threading it adds to commands/
// DimGeometry.h's BuildLinearDimensionGeometry/BuildRadiusDimensionGeometry.
// See PARITY_MAP.md's "Dimension styles" item, which this closes the
// units/precision half of (tolerance and extension-line/text-placement
// control, and the shared text/dimension style table, remain unattempted).
//
// Standalone, like dino8_test_g2_blend/dino8_test_variable_blend -
// DimGeometry.h only needs dino8_kernel's NurbsCurve, not a live Document or
// CommandContext. FormatNumber itself is declared extern by DimGeometry.h
// (defined in commands/CommandEngine.cpp, which pulls in app/Application.h
// and the whole GL/ImGui app) - this test supplies its own definition,
// identical to CommandEngine.cpp's, so it never has to link any of that
// (same isolation technique tests/test_shortcuts.cpp uses for imgui.h).
#include <cmath>
#include <cstdio>

#include "commands/DimGeometry.h"

namespace dino8::app {
// Deliberately identical to commands/CommandEngine.cpp's FormatNumber - see
// file comment above for why this test defines its own copy instead of
// linking the real one.
std::string FormatNumber(double v) {
  char buf[64];
  if (std::abs(v - std::round(v)) < 1e-9) {
    double r = std::round(v);
    if (r == 0) r = 0;
    std::snprintf(buf, sizeof(buf), "%.0f", r);
  } else {
    std::snprintf(buf, sizeof(buf), "%.4g", v);
  }
  return buf;
}
}  // namespace dino8::app

using dino8::app::BuildLinearDimensionGeometry;
using dino8::app::BuildRadiusDimensionGeometry;
using dino8::app::DimGlyphSpec;
using dino8::app::FormatDimensionNumber;
using dino8::app::LinearDimLayout;
using dino8::app::RadiusDimLayout;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}
}  // namespace

int main() {
  // FormatDimensionNumber in isolation.
  Check(FormatDimensionNumber(10.0, -1) == "10", "Auto (-1) on a round number matches plain FormatNumber (\"10\")");
  Check(FormatDimensionNumber(10.0, 2) == "10.00", "precision 2 on a round number keeps the trailing zeros (\"10.00\")");
  Check(FormatDimensionNumber(10.0, 0) == "10", "precision 0 on a round number shows no decimal point");
  Check(FormatDimensionNumber(3.14159, 2) == "3.14", "precision 2 rounds down correctly (\"3.14\")");
  Check(FormatDimensionNumber(3.14159, 4) == "3.1416", "precision 4 rounds up correctly (\"3.1416\")");
  Check(FormatDimensionNumber(-0.00001, 2) == "0.00", "a tiny negative value at low precision never prints \"-0.00\"");
  Check(FormatDimensionNumber(5.0, 20) == FormatDimensionNumber(5.0, 15), "an out-of-range precision is clamped, not UB (snprintf %.*f with a huge width)");

  // BuildLinearDimensionGeometry: exact-round length (10) so Auto (-1) and a
  // fixed precision produce visibly different text, proving `precision`
  // actually reaches the built label rather than being silently ignored.
  {
    LinearDimLayout L;
    L.plane = ON_Plane(ON_3dPoint::Origin, ON_3dVector::XAxis, ON_3dVector::YAxis);
    L.horizontal = true;
    L.offset = 5.0;
    std::vector<dino8::kernel::NurbsCurve> curves;
    DimGlyphSpec text;
    std::map<std::string, std::string> tags;
    double len = 0;
    Check(BuildLinearDimensionGeometry(Point3d(0, 0, 0), Point3d(10, 0, 0), L, 1.0, curves, text, tags, &len),
          "BuildLinearDimensionGeometry succeeds for a 10-unit horizontal span");
    Check(std::fabs(len - 10.0) < 1e-9, "measured length is 10");
    Check(text.text == "10", "default (-1) precision: label is the plain \"10\"");

    curves.clear();
    Check(BuildLinearDimensionGeometry(Point3d(0, 0, 0), Point3d(10, 0, 0), L, 1.0, curves, text, tags, &len, 2),
          "BuildLinearDimensionGeometry succeeds with precision=2");
    Check(text.text == "10.00", "precision=2: label is \"10.00\", not the plain \"10\"");

    // Internal round-trip tags stay full precision regardless of display
    // precision - UpdateDimensions must always replay from exact geometry.
    Check(tags["DimOffset"] == "5", "DimOffset tag is untouched by display precision (still plain FormatNumber)");
  }

  // BuildRadiusDimensionGeometry: same idea for Radius/Diameter, and checks
  // the "R "/"D " prefix survives alongside the now-fixed-precision value.
  {
    RadiusDimLayout L;
    L.diameter = false;
    L.plane = ON_Plane(ON_3dPoint::Origin, ON_3dVector::XAxis, ON_3dVector::YAxis);
    L.dir = Vector3d(1, 0, 0);
    std::vector<dino8::kernel::NurbsCurve> curves;
    DimGlyphSpec text;
    std::map<std::string, std::string> tags;
    double val = 0;
    Check(BuildRadiusDimensionGeometry(Point3d(0, 0, 0), 5.0, L, 1.0, curves, text, tags, &val),
          "BuildRadiusDimensionGeometry succeeds for radius 5");
    Check(text.text == "R 5", "default (-1) precision: radius label is \"R 5\"");

    curves.clear();
    Check(BuildRadiusDimensionGeometry(Point3d(0, 0, 0), 5.0, L, 1.0, curves, text, tags, &val, 3),
          "BuildRadiusDimensionGeometry succeeds with precision=3");
    Check(text.text == "R 5.000", "precision=3: radius label is \"R 5.000\"");

    L.diameter = true;
    curves.clear();
    Check(BuildRadiusDimensionGeometry(Point3d(0, 0, 0), 5.0, L, 1.0, curves, text, tags, &val, 2),
          "BuildRadiusDimensionGeometry(diameter) succeeds with precision=2");
    Check(text.text == "D 10.00", "diameter label doubles the radius and keeps the fixed precision (\"D 10.00\")");
  }

  if (failures) std::printf("%d FAILED\n", failures);
  else std::printf("all passed\n");
  return failures ? 1 : 0;
}
