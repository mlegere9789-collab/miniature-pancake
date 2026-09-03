#pragma once

#include <opennurbs.h>

#include "dino8/kernel/surface.h"

namespace dino8::kernel {

// Wraps ON_Brep. Chunk 1 only needs enough to construct a single-face
// B-rep from a NURBS surface and round-trip it through file I/O — the
// boolean engine (chunk 2) is what actually exercises ON_Brep's topology
// operations, so this stays minimal on purpose rather than guessing at
// an API that chunk hasn't specified yet.
class Brep {
 public:
  // Builds a one-face B-rep whose face is exactly `surface` (untrimmed).
  static Brep FromSurface(const NurbsSurface& surface);

  int FaceCount() const;

  const ON_Brep& raw() const { return brep_; }
  ON_Brep& raw() { return brep_; }

 private:
  ON_Brep brep_;
};

}  // namespace dino8::kernel
