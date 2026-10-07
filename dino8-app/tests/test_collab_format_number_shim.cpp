// Link-only shim for dino8_test_collab: io/File3dm.cpp's hatch-pattern
// export path (BuildPatternHatch, drafting/HatchBuild.h, included inline
// into File3dm.cpp's own translation unit) calls dino8::app::FormatNumber,
// and its dimension-import path (commands/DimGeometry.h's FormatMeasurement,
// included the same way) calls both FormatNumber and DecimalComma -
// normally defined in commands/CommandEngine.cpp, which this minimal,
// GUI-free test target deliberately does not link (CommandEngine.cpp pulls
// in the full Application/Viewport dependency chain for formatting helpers
// this test never actually exercises - test_collab.cpp never touches hatch
// patterns or dimensions). Byte-for-byte the same FormatNumber as
// CommandEngine.cpp's own; if that one's rounding/formatting rule ever
// changes, this copy should change with it. DecimalComma has no backing
// g_decimal_comma global here (this link-only shim has no app state at
// all), so it always reports the default (off) - FormatMeasurement only
// consults it for the fixed-precision path, which this test's own
// DimLinear/DimRadius default-style import never takes (precision stays
// -1), so the stubbed value is never actually observed.
#include <cmath>
#include <cstdio>
#include <string>

namespace dino8::app {

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

bool DecimalComma() { return false; }

}  // namespace dino8::app
