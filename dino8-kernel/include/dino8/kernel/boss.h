#pragma once

#include <vector>

#include "dino8/kernel/boolean_general.h"
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
// bosses on a sphere along every tilted axis tried (six), a round
// boss on a torus's outer equator, and round and rectangular bosses on a cone.
// The engine's polyline approximation of the intersection edge leaves the added
// volume of a small round boss up to 0.7% (sphere) / 1.1% (torus) high; those
// bounds, not exactness, are what the tests pin. CONE: added_volume used to be 6%
// high (round, seam side) / 9% low (rectangle) because the trimmed apex-singular
// cone face was integrated by ear clipping; VolumeOfSealedSolid() now integrates
// a curved trimmed face by Green's theorem (see brep.cpp), and the cone cases
// agree with their quadratures to 0.2%.
// A rectangular boss on a torus fuses at every azimuth, including azimuth 0 where the footprint straddles the seam CORNER of the
// torus's (u, v) domain (the engine refuses that one position, "3 or more fragment loops"): a refused fusion is retried on copies of
// a host that is a single untrimmed sphere or torus rotated about its axis (same solid, seam elsewhere; see src/host_reseam.h). A
// refused CURVE profile is also retried with its seam moved to 7 other positions of its domain (1/4, 1/2, 3/4, 0.1, 0.9, 0.37, 0.63):
// the extruded wall's seam always crosses the intersection curve once and leaves a zero-length edge there, and whether sealing copes
// depends on where. A host with no volume of its own (a loose-face solid from Brep::FromMixedFaces) is accepted: added_volume is then
// the boss minus the exact volume of boss ^ host. Refusals pass the engine's cause through.
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

// Pocket following a curved face (the cut counterpart of MakeBoss; parity-map "Wrap/emboss onto curved faces" - EmbossProfile only
// handled a profile against a planar or unconformed surface). A closed planar `profile` is swept `depth` along `direction` (which
// points INTO the material, MakeHole()'s convention) and subtracted from `solid`. The profile plane is the pocket's ENTRY and must lie
// OUTSIDE the material over the whole footprint (the caller lifts it above the curved face); the floor is the entry plane moved `depth`
// along `direction` and must lie INSIDE it, so the pocket has a flat floor and walls that end exactly on the curved face. Both are
// checked by sampling the outline with ContainsPointExact; the first sample on the wrong side is named by its coordinates.
//
// The cut is BooleanCombineGeneralSealed(solid, tool, Difference): the result is ON_Brep::IsValid() and IsSolid(), or the call throws.
// Same profile kinds, argument checks, refusal style, curve-seam retries and sphere/torus host re-seaming as MakeBoss. A host with no
// volume of its own is accepted: removed_volume is then the exact volume of tool ^ host.
struct PocketResult {
  Brep brep;
  double tool_volume = 0;     // exact volume of the swept pocket tool (entry to floor)
  double removed_volume = 0;  // host volume - result volume (the part of the tool inside the host)
};

PocketResult MakePocket(const Brep& solid, const NurbsCurve& profile, Vector3d direction, double depth);
PocketResult MakePocket(const Brep& solid, const std::vector<Point3d>& profile_ring, Vector3d direction, double depth);

// Emboss / deboss a closed planar profile (optionally with HOLES / islands) onto a CURVED face (parity-map "Feature operations" -
// "Emboss/deboss": EmbossProfile()/EmbossProfileWithHoles() place their tool against a PLANAR surface, with the profile's own plane
// standing for the surface and only a 1e-3-diagonal margin of overlap; on a sphere or cylinder wall that plane touches the surface in
// a point or a line, so there is nothing to fuse onto or cut into). This one keeps EmbossProfile()'s sign convention - `direction`
// points INTO the material - and takes the profile plane as a REFERENCE plane, exactly as MakeBoss()/MakePocket() do:
//  - EmbossMode::Emboss: the profile plane is the raised feature's BASE and must lie inside the material over the whole footprint;
//    the feature grows `depth` against `direction` (outward), its walls ending exactly ON the curved face (MakeBoss).
//  - EmbossMode::Deboss: the profile plane is the engraving's ENTRY and must lie outside the material; the floor is `depth` along
//    `direction` and must lie inside it (MakePocket).
// `holes` are closed planar curves in the same plane as `outer`, each inside it and disjoint from the others (a lettering counter,
// an island): they stay FLUSH with the host's own curved face - in an emboss no material is added inside a hole, in a deboss no
// material is removed inside one. The tool is one solid ExtrudeProfileWithHoles() (the ring-shaped prism), fused with / cut from the
// host by the same sealed boolean MakeBoss()/MakePocket() use, so the result is ON_Brep::IsValid() and IsSolid(), or the call throws
// the engine's own cause (never a degraded body). Same checks, seam retries and sphere/torus host re-seaming as MakeBoss(); `holes`
// empty simply forwards to MakeBoss()/MakePocket().
//
// Throws std::invalid_argument for the MakeBoss() argument errors, a hole that is open, not planar, not coplanar with `outer`, not
// inside `outer`, or overlapping another hole; std::runtime_error naming the measured cause otherwise.
struct EmbossResult {
  Brep brep;
  double tool_volume = 0;    // exact volume of the swept ring/profile tool (reference plane to top/floor)
  double volume_change = 0;  // result volume - host volume: positive for an emboss, negative for a deboss
};

EmbossResult EmbossOnCurvedFace(const Brep& solid, const NurbsCurve& outer, const std::vector<NurbsCurve>& holes, Vector3d direction,
                                double depth, EmbossMode mode);

}  // namespace dino8::kernel
