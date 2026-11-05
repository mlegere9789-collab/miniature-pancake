#pragma once

#include <vector>

#include "dino8/kernel/brep.h"

namespace dino8::kernel {

// Rib / web feature (parity-map "kernel: Feature operations" - "Rib", which
// was only the app's BossRibCommand mesh-boolean path before this).
//
// A rib is a thin planar web: a simple polygon `profile` (the rib's side
// view, listed as an open vertex ring - do not repeat the first vertex)
// swept `thickness` through its own plane (centered on it when `symmetric`,
// else from the plane toward the profile's Newell normal) and fused onto
// `solid`. The caller places the profile so its body-side edge sinks a little
// INTO the material; the rib is exactly what the profile sweeps, nothing is
// extended or trimmed for the caller.
//
// The tool is built from true planar faces (Brep::FromPlanarFaces), NOT from
// Brep::Extrude(): Extrude's flat end caps are FAN-capped (a singular side),
// and measured here that makes BooleanCombineGeneral throw "an edge is
// claimed by 3 or more fragment loops" for a web that crosses the body's
// face sideways, while the identical box-shaped web built from planar faces
// fuses cleanly. The fusion is BooleanCombineGeneralSealed(solid, tool,
// Union), so the result is ON_Brep::IsValid() and IsSolid() or the call
// throws.
//
// Throws std::invalid_argument for: a solid with no faces, a non-positive or
// non-finite thickness, fewer than 3 profile vertices, a profile that is not
// planar (any vertex further than 1e-6 x its extent from the best-fit plane),
// has (near) zero area or repeats a vertex, or self-intersects. Throws
// std::runtime_error naming the measured cause when the rib cannot be fused:
// its bounding box misses the solid's, the engine/sealing refuses, the fused
// body adds no measurable volume (the rib lies inside the solid or only
// grazes it), or the fused volume exceeds solid + tool (impossible, so the
// result is rejected rather than returned).
struct RibResult {
  Brep brep;                 // solid + rib, IsValid() and IsSolid()
  double tool_volume = 0;    // exact volume of the swept web
  double added_volume = 0;   // brep volume - solid volume (the part outside the solid)
};

RibResult MakeRib(const Brep& solid, const std::vector<Point3d>& profile, double thickness, bool symmetric = true);

// Several webs of one thickness fused in ONE boolean (a compound tool). Measured: fusing a second web onto the sealed
// result of a first MakeRib() call is refused by the engine ("not a closed solid after sewing"), while the same two webs
// as one compound tool fuse. The webs must have pairwise separate bounding boxes (std::invalid_argument otherwise:
// crossing / touching ribs are not supported). tool_volume is the sum of the webs'.
RibResult MakeRibs(const Brep& solid, const std::vector<std::vector<Point3d>>& profiles, double thickness,
                   bool symmetric = true);

// A rib flush with the body's end face. MakeRib() of a web whose end lies exactly ON a face of the solid is refused by the engine
// ("an edge is claimed by 3 or more fragment loops": coincident faces). MakeRibFlush() takes a profile that OVERHANGS the end face
// and trims it back: it fuses the overhanging web (MakeRib), then subtracts the half-space beyond each `planes` entry (a slab built
// from planar faces) with BooleanCombineGeneralSealed(Difference). Measured: the cutter's face coinciding with the body's end face
// is handled by that Difference. Each plane is a point on the body's end face and its OUTWARD normal; the solid must lie behind it
// (checked against every face surface's control points, a safe convex-hull bound, so a curved
// body whose control net pokes past the plane is refused with std::invalid_argument). added_volume is the final volume minus the solid's. Refusals as for MakeRib, plus a trim that the engine
// refuses.
struct RibFlushPlane {
  Point3d point;
  Vector3d normal;
};
RibResult MakeRibFlush(const Brep& solid, const std::vector<Point3d>& profile, double thickness,
                       const std::vector<RibFlushPlane>& planes, bool symmetric = true);

}  // namespace dino8::kernel
