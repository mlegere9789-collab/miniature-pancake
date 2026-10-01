// General-purpose "build a trimmed Brep face from a surface plus loops of
// (2D parameter-space, 3D model-space) curve pairs" machinery. This is the
// same construction the IGES/STEP importer (src/io/FileIgesStep.cpp) has
// long used to turn ADVANCED_FACE / TrimmedSurface entities into real
// ON_Brep faces (vertices, edges, trims, seam sharing, singular trims at a
// degenerate corner) - moved here, unchanged, so other real trimming work
// (ConnectSrf's general non-planar case; any future command that needs to
// cut a NURBS surface along an arbitrary 3D curve lying on it) can reuse the
// exact same, already-exercised code instead of re-deriving Brep topology
// construction from scratch.
#pragma once

#include <opennurbs.h>

#include <vector>

namespace dino8::app {

// One trim of a loop: the 2D parameter-space curve (dimension 2, matching
// the 3D curve's own parametrisation/direction) and the 3D model-space
// curve it corresponds to. Both run in the loop's own direction.
struct LoopSeg {
  ON_NurbsCurve c2;
  ON_NurbsCurve c3;
};

// Adds one face (srf, plus its trim loops) to `brep`. `loops[0]` is the
// outer loop; any further entries are inner (hole) loops. Builds vertices,
// edges and trims from each segment's matched (2D,3D) curve pair, sharing a
// vertex/edge with anything already added to this face within `tol`
// (letting two adjacent loop segments share an edge, as at a seam), and
// turning a segment whose 3D curve collapses to a point into a singular
// trim rather than a degenerate zero-length edge. Returns the new face's
// index, or -1 if a loop was empty. `srf`'s ownership passes to `brep`
// (adopted via AddSurface) either way.
int AddTrimmedFace(ON_Brep& brep, ON_NurbsSurface* srf, const std::vector<std::vector<LoopSeg>>& loops, double tol);

// Finishes a brep's trim/edge bookkeeping once its faces and loops are all
// in place: classifies each trim as boundary/seam/mated from how many trims
// its edge carries, sets trim iso flags, then tolerances/boxes/flags.
void FinishBrepTrims(ON_Brep& b);

}  // namespace dino8::app
