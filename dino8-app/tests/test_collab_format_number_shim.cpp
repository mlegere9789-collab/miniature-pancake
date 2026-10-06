// Link-only shim for dino8_test_collab: io/File3dm.cpp's hatch-pattern
// export path (BuildPatternHatch, drafting/HatchBuild.h, included inline
// into File3dm.cpp's own translation unit) calls dino8::app::FormatNumber,
// normally defined in commands/CommandEngine.cpp - which this minimal,
// GUI-free test target deliberately does not link (CommandEngine.cpp pulls
// in the full Application/Viewport dependency chain for a formatting
// helper this test never actually exercises - test_collab.cpp never
// touches hatch patterns). Byte-for-byte the same implementation as
// CommandEngine.cpp's own FormatNumber; if that one's rounding/formatting
// rule ever changes, this copy should change with it.
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

}  // namespace dino8::app
