// The hatch pattern library: AutoCAD .pat patterns (ANSI, ISO, brick,
// honeycomb, dots...) loaded from data/hatchpatterns.pat plus any other
// *.pat file next to it, with a built-in copy as a fallback, and the line
// generator that clips a pattern to a set of planar boundary loops.
//
// The generator is plain geometry (no document): Hatch, HatchScale,
// SectionView and the Hatch panel's thumbnails all draw through it.
#pragma once

#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/types.h"

namespace dino8::app::drafting {

// One family of parallel lines in a pattern (one .pat line).
struct HatchFamily {
  double angle = 0;      // degrees
  double x0 = 0, y0 = 0; // origin (pattern units)
  double dx = 0, dy = 1; // offset between successive lines: along / across
  std::vector<double> dashes;  // + dash, - gap, 0 dot; empty = continuous
};

struct HatchPattern {
  std::string name;
  std::string description;
  std::vector<HatchFamily> families;
  bool IsSolid() const;  // the reserved SOLID pattern
};

// Parses .pat text; appends the patterns found (later definitions of the
// same name replace earlier ones). Returns the number of patterns read.
int ParsePatText(const std::string& text, std::vector<HatchPattern>& out, std::string* error = nullptr);

// The library: built-in patterns overlaid by data/hatchpatterns.pat and
// every other data/*.pat. `data_dirs` are searched in order for the first
// directory that exists; call Load once at startup (or after the user
// drops a file in) - the getters lazily load the built-ins otherwise.
class HatchLibrary {
 public:
  static HatchLibrary& Instance();
  void Load(const std::vector<std::string>& data_dirs);
  void Reload();
  const std::vector<HatchPattern>& Patterns();
  const HatchPattern* Find(const std::string& name);
  std::vector<std::string> Names();
  const std::string& SourceDir() const { return source_dir_; }
  // Adds (or replaces) a pattern parsed from a .pat file; returns the count read.
  int ImportFile(const std::string& path, std::string* error = nullptr);

 private:
  void EnsureLoaded();
  std::vector<std::string> data_dirs_;
  std::vector<HatchPattern> patterns_;
  std::string source_dir_;
  bool loaded_ = false;
};

// A planar polygon loop (no repeated closing point).
using Loop = std::vector<kernel::Point3d>;

// Pattern lines clipped (even-odd) to `loops`, which all lie on `plane`.
// `scale` multiplies every pattern length, `rotation` (degrees) is added
// to every family angle and `base` (projected on the plane) is where the
// pattern origin sits, so neighbouring hatches with the same base line up.
// Stops after `max_segments` segments (a density guard) and reports the
// truncation through `truncated` when given.
std::vector<kernel::NurbsCurve> HatchPatternCurves(const HatchPattern& pattern, const std::vector<Loop>& loops,
                                                   const ON_Plane& plane, double scale, double rotation,
                                                   kernel::Point3d base, size_t max_segments = 200000,
                                                   bool* truncated = nullptr);

// 2D segments (x0,y0,x1,y1 in pattern units) of the pattern inside the
// square [0,size]^2 - for thumbnails.
std::vector<double> HatchPatternPreview(const HatchPattern& pattern, double size, double scale, double rotation,
                                        size_t max_segments = 4000);

}  // namespace dino8::app::drafting
