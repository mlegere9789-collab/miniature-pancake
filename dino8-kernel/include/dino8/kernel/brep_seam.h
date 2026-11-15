// SrfSeam on a face of a polysurface.
//
// Kept in its own small header (not brep.h, which nearly every translation unit includes) so adding it does not recompile the world.
#pragma once

#include <string>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/types.h"

namespace dino8::kernel {

// Moves the seam of face `face_index`'s surface in `direction` (0 = U, 1 = V) to parameter `t`. Supported for a face that is its whole surface
// (`Brep::FaceCoversWholeDomain()` and no ON_Brep loops: what Box(), Sphere(), Torus(), FromSurface() build), where there is no trim loop whose
// parameters would have to move with the seam, so replacing the surface is the whole job. The face's surface is converted to NURBS if it is not
// already, `NurbsSurface::ChangeSeam()` does the move (same contract: Ok; NoOpAlreadySatisfied for a `t` at the existing seam; Failed for an open
// direction or a `t` outside the domain; the new domain is [t, t + L]; the shape and parameterization are unchanged), and the result is stored back -
// in place when only this face uses the surface, as a fresh surface otherwise.
//
// Result::Failed and the Brep untouched, with a plain-language reason in `*reason` when given, for a face with real loop topology (welded solids,
// boolean results, trimmed faces: their trim curves and edges would have to be re-seamed with it, which is not attempted) or one trimmed by the
// kernel's pseudo-trim side tables. Throws std::invalid_argument for a face index out of range or a direction outside 0/1.
Result ChangeFaceSeam(Brep& brep, int face_index, int direction, double t, std::string* reason = nullptr);

}  // namespace dino8::kernel
