// count_limits.h - guards for commands that turn a typed number into a loop count (Divide, Array, PointGrid, Helix...).
// A typed 1e8 or 1e12 used to hang the app or overflow `static_cast<int>(double)` (undefined behaviour above INT_MAX).
#pragma once

#include <cmath>
#include <string>

namespace dino8::app {

// Largest number of objects/points one command may create from a typed count.
constexpr double kMaxCreatedItems = 100000;
// Largest Helix/Spiral turn count (typed Turns, or axis length / Pitch).
constexpr double kMaxTurns = 1000;
// Largest distance a typed extrusion may reach. ExtrudeSrf 1e308 on a 10 unit box never returned.
constexpr double kMaxExtrudeDistance = 1e8;

// True and `out` set when `v` is a finite whole-or-fractional number in [lo, hi]; the fraction is truncated like the
// old static_cast<int> did. Otherwise `why` receives a message naming the allowed range.
inline bool CountInRange(double v, double lo, double hi, int& out, std::string& why) {
  if (!std::isfinite(v) || v < lo || v > hi) {
    why = "must be a number from " + std::to_string(static_cast<long long>(lo)) + " to " + std::to_string(static_cast<long long>(hi));
    return false;
  }
  out = static_cast<int>(v);
  return true;
}

}  // namespace dino8::app
