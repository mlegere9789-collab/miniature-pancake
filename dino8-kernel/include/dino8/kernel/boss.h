#pragma once

#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"

namespace dino8::kernel {

// Boss on a (possibly curved) face (parity-map "kernel: Feature operations" -
// "Boss following a curved surface", previously only the app's BossRibCommand
// mesh-boolean path).
//
// A boss is a closed planar `profile` swept `height` along `direction` and
// fused onto `solid`. The profile plane is the boss's BASE and must lie inside
// the material over the whole footprint (the caller sinks it below the face,
// however curved it is); the free top is the base plane moved `height` along
// `direction`. Because the fused body is the Boolean union, the boss's side
// walls end exactly ON the curved face (cylinder wall, sphere, ...) - the base
// "follows" the face with no gap and no approximation of the face.
//
// Profile kinds: a degree-1 closed NurbsCurve (a polygon) is swept as true planar
// faces (so the walls, which cross the host face, are planes); any other closed
// planar curve (a circle, an ellipse, a spline outline) goes through
// Brep::Extrude(). The vector overload takes an open vertex ring.
//
// The fusion is BooleanCombineGeneralSealed(solid, tool, Union): the result is
// ON_Brep::IsValid() and IsSolid(), or the call throws. Measured scope: round
// and rectangular bosses on a cylinder wall and on a sphere (pole axis), round
// bosses on a sphere along every tilted axis tried except (-1,-2,2), a round
// boss on a torus's outer equator, and round and rectangular bosses on a cone.
// The engine's polyline approximation of the intersection edge leaves the added
// volume of a small round boss up to 0.7% (sphere) / 1.1% (torus) high; those
// bounds, not exactness, are what the tests pin. CONE: the body is right (point
// membership of a grid agrees with the construction everywhere outside a 0.03
// band) but the REPORTED added volume of the cone cases was measured 6% high
// (round) and 9% low (rectangle), the divergence-theorem integral of the
// apex-singular trimmed cone face; do not trust added_volume for a cone host.
// A rectangular boss on a torus is refused (3-loop non-manifold), and a round
// boss on a sphere along (-1,-2,2) is refused (seam trim without an iso flag),
// even after the profile's seam is moved by 1/4, 1/2 and 3/4 of its domain, which
// is the retry a refused CURVE profile gets (a round boss on a sphere along
// (-1,2,2) needed it). Refusals pass the engine's cause through.
//
// Throws std::invalid_argument for a solid with no faces, a non-positive or
// non-finite height, a zero direction, an open or non-planar profile, a
// direction lying in the profile plane (|sin| < 1e-3), or a degenerate polygon;
// throws std::runtime_error naming the measured cause when the base is not
// embedded (the first sampled base point that is NOT inside the solid is named
// with its coordinates), when the fusion is refused, or when the fused volume
// grows by nothing or by more than the boss's own volume.
struct BossResult {
  Brep brep;
  double tool_volume = 0;   // exact volume of the swept boss tool (base to top)
  double added_volume = 0;  // fused volume - solid volume (the part outside the solid)
};

BossResult MakeBoss(const Brep& solid, const NurbsCurve& profile, Vector3d direction, double height);
BossResult MakeBoss(const Brep& solid, const std::vector<Point3d>& profile_ring, Vector3d direction, double height);

}  // namespace dino8::kernel
