#pragma once

#include <vector>

#include "dino8/kernel/brep.h"

namespace dino8::kernel {

// A counterbore hole: a coaxial pair of cylindrical drilling cutters - a
// narrow DRILL cylinder (the hole's own through/blind bore) plus a wider,
// shallower COUNTERBORE cylinder recessed at the starting face, sharing
// the same axis and starting point, e.g. the socket a hex-head bolt sits
// in above its own shaft hole.
//
// Not a new boolean engine: composed as a single compound cutter Brep -
// two coaxial Brep::CylindricalFace entries covering ADJACENT,
// non-overlapping axial ranges (the wide counterbore wall over
// [0, counterbore_depth], the narrow drill wall over
// [counterbore_depth, drill_depth]) - fed into ONE
// BooleanCombineMixed(..., BooleanOp::Difference) pass against `solid`
// (see boolean.h's own BooleanCombineMixed doc comment for that
// pipeline). Deliberately NOT two full-length, axially-overlapping
// cylinders composed via two sequential Difference passes (in either
// order) or via a prior Union of two overlapping bare cylinders - both
// were tried and both fail (see features.cpp's own doc comment on
// CounterboreHole for exactly how); adjacent, non-overlapping segments
// sidestep the issue entirely, since `solid`'s own starting face is then
// crossed by only the counterbore's own (outer) circle, `solid`'s own far
// face (if the drill reaches it) by only the drill's own (inner) circle,
// and the step between the two cutter radii is a brand-new face this
// call synthesizes, not a second clip of an already-circular one.
//
// `origin` is the point on `solid`'s own surface where the hole starts;
// `axis` points INTO the material (the direction the hole is drilled) -
// it need not be a unit vector, but must be nonzero. `drill_radius`/
// `drill_depth` describe the narrow bore exactly like a plain single-
// cylinder hole (pass a `drill_depth` that reaches all the way through
// `solid` for a through hole, or less for a blind one); `counterbore_radius`/
// `counterbore_depth` describe the wider recess at the starting face.
//
// `axis` need not be perpendicular to `solid`'s own surface at `origin` -
// an oblique hole is built the same way - but a steep-enough tilt
// relative to `counterbore_radius`/`drill_radius` can still hit
// BooleanCombineMixed's own pre-existing, disclosed "oblique plane's own
// intersection with a cylindrical face enters/exits across only part of
// the swept angle" non-monotonic-crossing limitation (confirmed directly:
// this is not specific to this feature's own composition, and margin
// past the surface does not avoid it), in which case this throws
// whatever BooleanCombineMixed itself throws for that case.
//
// Throws std::invalid_argument if `axis` is zero-length, if either
// radius or depth is not strictly positive, if `counterbore_radius` is
// not strictly greater than `drill_radius` (otherwise the "wider" cutter
// does not actually widen anything, so this would not be a counterbore),
// or if `counterbore_depth` is not strictly less than `drill_depth` (a
// counterbore needs an actual narrower bore beyond its own recess).
Brep CounterboreHole(const Brep& solid, Point3d origin, Vector3d axis, double drill_radius, double drill_depth,
                      double counterbore_radius, double counterbore_depth);

// One plain round hole recognized on an existing solid (parity-map
// "Feature recognition" - the "hole" half of its own "still no
// hole/boss/pocket recognition" gap): the geometric inverse of
// MakeHole()/CounterboreHole() - given a Brep that already HAS a
// cylindrical bore cut into it (by any means - this reads back only the
// finished shape, not any feature history), recovers the same
// (origin, axis, radius, depth, through) parameters a caller could feed
// straight back into MakeHole() to reproduce it.
//
// `origin` is the point on the axis at the hole's own entry rim (where it
// meets the surface it was drilled from) - the same point MakeHole()'s own
// `center` parameter describes, not the axis's arbitrary base point some
// other representation might use. `axis` is a unit vector pointing INTO
// the material, matching MakeHole()'s own "points into the material"
// convention. `depth` is the axial distance from `origin` to the hole's
// own far end - the flat bottom of a blind hole, or the exit rim of a
// through hole (still a finite, measured number in the through case too,
// unlike MakeHole()'s own `through=true`, which ignores its `depth`
// argument entirely and drives the cutting tool arbitrarily far past
// `solid`'s bounding box instead).
struct HoleFeature {
  Point3d origin;
  Vector3d axis;
  double radius = 0.0;
  double depth = 0.0;
  bool through = false;

  // Index into `solid.raw().m_F` (the ON_Brep this was recognized from) of
  // the cylindrical face this feature came from - lets a caller correlate
  // a returned HoleFeature back to the specific face of its own input,
  // e.g. to select it in a UI.
  int face_index = -1;
};

// Scans every face of `solid` for a genuine round hole: a face whose
// underlying surface fits a full (closed, 2*pi) cylinder (`ON_Surface::
// IsCylinder()` - so a PARTIAL cylindrical patch, e.g. a fillet's own
// rolling-ball wall, is never reported: it isn't itself a hole, whatever
// solid it happens to be attached to) AND whose face orientation is
// CONCAVE - its own outward-facing normal (`ON_Surface::NormalAt()`,
// flipped per `ON_BrepFace::m_bRev` exactly like every other
// orientation-aware read in this kernel, e.g. BossRibCommand's own
// ProjectToBase in dino8-app) points TOWARD the cylinder's own axis rather
// than away from it. A convex full cylinder (a boss/pin sticking OUT of
// `solid`, the opposite winding) is deliberately not reported here - see
// this function's own "Still partial" note below.
//
// For each recognized cylindrical face, its own axial extent [t_min,
// t_max] is read directly off every point of every edge in every one of
// its loops (a plain global min/max along the axis - no loop/trim
// ADJACENCY reasoning at all, so this is immune to BooleanCombineGeneral's
// own disclosed fragmentation of a rim into many short polyline segments,
// or even a genuinely naked one - see MakeHole()'s own doc comment on the
// "tool's far end floats entirely inside the target" entry-rim gap). Each
// of the face's own two ends is then classified OPEN (the bore continues
// past it, into open air) or CAPPED (more of `solid`'s own material blocks
// it) by tessellating `solid` once (`Brep::TessellateToClosedMesh()`) and
// asking `Mesh::ContainsPoint()` about a point a small margin PAST that
// end, ON the cylinder's own axis - deliberately at radius 0 from the
// bore, nowhere near the specific radius-== -hole-radius locus the
// entry-rim gap above sits at, so this sidesteps that disclosed gap
// entirely rather than working around it. A hole with one open end and
// one capped end is `through = false`, `depth` the axial distance from the
// open end to the capped one. A hole with BOTH ends open is `through =
// true`, `depth` the axial distance end-to-end (a real, finite
// measurement, even though MakeHole() itself never needs one for a
// through hole - see HoleFeature's own doc comment). A hole found with
// BOTH ends capped (an entirely enclosed cylindrical cavity, never
// reaching any outer face of `solid` at all) is not a hole feature in the
// Rhino/SolidWorks "Hole" sense - nothing drilled it from outside - and is
// silently skipped, the same way a convex/boss cylinder is.
//
// Every recognized face_index is independent: a solid with several holes
// (through, blind, or a mix) is reported as several HoleFeature entries,
// in face-index order, not merged or deduplicated.
//
// Still partial, not a full "hole/boss/pocket" recognizer: a genuinely
// convex cylindrical boss/pin and a general (non-cylindrical) pocket are
// both out of scope here entirely - see this function's own "concave
// cylinder only" filter above; no attempt is made here to recognize a
// COUNTERBORE/COUNTERSINK's own second, wider cylindrical/conical step as
// part of the SAME feature - each cylindrical wall segment of a stepped
// hole comes back as its own separate HoleFeature; and the open/capped
// classification inherits Mesh::ContainsPoint()'s own disclosed "closed,
// consistently-oriented mesh" precondition (mesh.h) - a `solid` whose
// tessellation is itself unreliably closed somewhere ELSE (unrelated to
// the hole being recognized) could in principle still misclassify, though
// no such case is known.
std::vector<HoleFeature> RecognizeHoles(const Brep& solid);

// One boss/pin recognized on an existing solid (parity-map "Feature
// recognition" - the "boss" half of its own "still no hole/boss/pocket
// recognition" gap, closed alongside RecognizeHoles() above): the
// geometric mirror of a HoleFeature - a CONVEX full cylinder, material
// INSIDE the wall rather than outside it (a rod/peg sticking OUT of
// `solid`, RecognizeHoles()'s own disclosed negative-control case), the
// opposite winding RecognizeHoles() itself rejects.
//
// `origin` is the point on the axis where the boss meets the body it
// emerges from (the base, embedded in `solid`'s own bulk) - the point a
// hypothetical "MakeBoss(solid, origin, axis, radius, height)" call would
// take as its own starting point. `axis` is a unit vector pointing AWAY
// from the material, from the base toward the free tip - the opposite
// sense from HoleFeature's own "into the material" convention, since a
// boss is additive material growing outward rather than a cut going
// inward. `height` is the axial distance from `origin` to the boss's own
// far end - the flat/round tip of an ordinary boss that ends in open air,
// or (for a `through` boss - see RecognizeBosses()'s own doc comment
// below for why this is a free-standing-rod case, NOT a peg embedded
// partway through a wall) the far end of a candidate whose probe points
// come back open on both sides.
struct BossFeature {
  Point3d origin;
  Vector3d axis;
  double radius = 0.0;
  double height = 0.0;
  bool through = false;

  // Index into `solid.raw().m_F`, same convention as HoleFeature::face_index.
  int face_index = -1;
};

// Scans every face of `solid` for a genuine round boss/pin: a face whose
// underlying surface fits a full (closed, 2*pi) cylinder, same
// `IsCylinder()`/`IsClosed(0)` gate RecognizeHoles() itself uses, but
// CONVEX rather than concave - the exact opposite half of the same
// concavity test (see RecognizeHoles()'s own doc comment for the shared
// axial-extent-by-loop-point-min/max construction, reused unchanged
// here: it is immune to loop/trim adjacency and fragmentation by
// construction, regardless of which winding the face has).
//
// Each end of a candidate face is classified the same way RecognizeHoles()
// classifies a bore's own two ends - tessellate `solid` once
// (`Brep::TessellateToClosedMesh()`) and ask `Mesh::ContainsPoint()` about
// a point a small margin PAST that end, on the axis - but the two
// resulting booleans are read with the boss's own inverted meaning: an
// end where the point just past it falls OUTSIDE `solid` is this boss's
// own FREE end (open air - nothing supports it further out); an end
// where that point falls INSIDE `solid` is an ATTACHED end (the boss is
// still backed by more of the body's own material beyond it, i.e. this
// is the base, not the tip). A boss with one free end and one attached
// end is `through = false`, the ordinary "peg sticking out of one face"
// case - `origin` sits at the attached end, `axis` points toward the
// free tip, `height` the axial distance between them. A boss found with
// BOTH ends free is `through = true`, `origin`/`axis` pinned to one end
// arbitrarily (face-index order, same convention HoleFeature's own
// through case already uses) and `height` the full end-to-end span - NOT
// a peg embedded partway through a thin wall and protruding out both
// sides (that shape's own wall is not a single candidate face at all:
// the embedded middle section is interior to `solid`, not boundary, so a
// real boolean union of a box and such a peg leaves TWO disjoint convex
// cylindrical faces, one on each protruding side, each independently
// `through = false` - the true `through = true` case is a candidate not
// backed by material at EITHER end within the probe margin, e.g. a
// free-standing rod barely touching `solid` or not attached to it at
// all). A candidate found with BOTH ends attached (a cylindrical rod
// entirely embedded in `solid`'s own bulk, exposed nowhere) is not a
// recognizable boss feature - nothing about it is visibly "sticking out"
// - and is silently skipped, mirroring RecognizeHoles()'s own "both ends
// capped" skip for an entirely enclosed cavity.
//
// Still partial, not a full "hole/boss/pocket" recognizer, for the same
// reasons RecognizeHoles() itself discloses: a general (non-cylindrical)
// pocket or boss shape is out of scope entirely; a counterbore/
// countersink's own second step (see RecognizeCounterboreHoles() below
// for the hole-side equivalent) has no boss-side analogue implemented
// here; and the open/attached classification inherits
// Mesh::ContainsPoint()'s own disclosed "closed, consistently-oriented
// mesh" precondition - CONFIRMED, not just hypothetical, for one specific
// case (dino8_scratch_test): a boss built by BooleanCombineGeneral()
// Union-ing a separate tool onto an existing solid, with the tool's own
// base cap backed off entirely inside the target (the natural way to
// build one - MakeHole()'s own margin trick, mirrored for Union), leaves
// TessellateToClosedMesh() non-closed and Mesh::ContainsPoint() wrong
// across the WHOLE embedded span, not merely near the entry rim the way
// MakeHole()'s own disclosed Difference-side "floating cap" gap is -
// a real Union-side counterpart to that gap, not yet fixed. A boss built
// any other way (e.g. as one originally-modeled solid, not composed via
// this kernel's own general boolean engine) is unaffected.
std::vector<BossFeature> RecognizeBosses(const Brep& solid);

// One compound counterbore feature recognized on an existing solid
// (parity-map "Feature recognition" - closes the "no attempt is made
// here to recognize a COUNTERBORE/COUNTERSINK's own second, wider
// cylindrical/conical step as part of the SAME feature" gap
// RecognizeHoles() itself discloses, for the CYLINDRICAL-step case - i.e.
// a real counterbore, the geometric inverse of CounterboreHole()/
// MakeCounterboreHole()): two coaxial concave full-cylinder faces over
// ADJACENT, non-overlapping axial ranges - a wide recess wall immediately
// followed by a narrower pilot-bore wall sharing the same axis line -
// reported as ONE feature instead of RecognizeHoles()'s own two
// independent HoleFeature entries for the same cut.
//
// `origin`/`axis` describe the counterbore's own entry point exactly like
// MakeCounterboreHole()'s own `center`/`axis` parameters (axis pointing
// INTO the material, matching HoleFeature's convention, not
// BossFeature's). `counterbore_radius`/`counterbore_depth` describe the
// wide recess; `drill_radius`/`drill_depth` describe the pilot bore,
// `drill_depth` measured from `origin` (the counterbore's own entry
// point), not from the step - the same "total depth from the entry
// surface" convention MakeCounterboreHole()'s own `bore_depth` parameter
// already uses, so a round-trip is a straight
// `MakeCounterboreHole(fresh_solid, origin, axis, drill_radius,
// drill_depth, through, counterbore_radius, counterbore_depth)` call.
// `through` reflects the pilot bore's own far end, exactly like
// HoleFeature::through.
struct CounterboreFeature {
  Point3d origin;
  Vector3d axis;
  double counterbore_radius = 0.0;
  double counterbore_depth = 0.0;
  double drill_radius = 0.0;
  double drill_depth = 0.0;
  bool through = false;

  // Face indices of the two walls this feature was merged from - the wide
  // recess wall and the narrower pilot-bore wall, in that order.
  int counterbore_face_index = -1;
  int drill_face_index = -1;
};

// Scans every face of `solid` the same way RecognizeHoles() does, but
// instead of reporting every concave full cylinder as its own
// independent hole, looks specifically for PAIRS that share the same
// axis LINE (parallel axis directions, and colinear axis reference
// points - not merely parallel: two holes drilled side by side on
// parallel axes must not merge) where one face's own far end coincides,
// in 3D, with the other's own near end (an adjacent, non-overlapping
// step - CounterboreHole()'s/MakeCounterboreHole()'s own construction,
// see their doc comments), and the two radii genuinely differ (the wider
// one first, along the entry direction, then the narrower pilot bore -
// the counterbore recess CANNOT be the deeper, narrower segment). Only
// such merged pairs are reported here; a plain single-radius hole
// (RecognizeHoles()'s own domain) is not repeated in this function's
// output, and a merged pair's own two constituent faces are NOT also
// expected to disappear from RecognizeHoles()'s own separate output -
// this is the same one-capability-two-vocabularies convention the parity
// map's own SplitByObjectCommand/DraftFacesConvexPlanar entries already
// use, not a bug.
//
// The merged step's own near end (the wide recess's own start, i.e. the
// entry surface) and far end (the narrow pilot bore's own far end) are
// classified open/capped with the exact same on-axis
// `Mesh::ContainsPoint()` test RecognizeHoles() itself uses, tessellating
// `solid` once and reusing it for every merged candidate.
//
// Still partial: only the CYLINDRICAL/CYLINDRICAL step case (a real
// counterbore) is recognized - a countersink's own conical step
// (MakeCountersinkHole()'s own frustum wall) is a different surface type
// entirely and is not merged here; a stepped hole with more than two
// radii (a counterbore followed by its own further pilot reduction) is
// not walked past the first pair; and, like RecognizeHoles() itself, a
// general (non-cylindrical) pocket is out of scope.
std::vector<CounterboreFeature> RecognizeCounterboreHoles(const Brep& solid);

// One segment of a multi-step chain recognized by
// RecognizeSteppedHoleChains() below - a single cylindrical wall's own
// radius/length/face_index, in entry-to-far order within the enclosing
// SteppedHoleChain (see that struct's own doc comment for what "length"
// is measured from for a step other than the first).
struct SteppedHoleStep {
  double radius = 0.0;
  double length = 0.0;
  int face_index = -1;
};

// A compound hole with THREE OR MORE coaxial cylindrical steps - the
// generalization of CounterboreFeature above (exactly two steps: a
// counterbore recess plus its own pilot bore) to an arbitrary chain
// length, closing RecognizeCounterboreHoles()'s own disclosed "a stepped
// hole with more than two radii (a counterbore followed by its own
// further pilot reduction) is not walked past the first pair" gap - e.g.
// a spot-face, then a counterbore recess, then a narrower pilot drill, or
// any other chain of adjacent same-axis cylindrical steps.
//
// `origin`/`axis` describe the chain's own entry point and into-material
// direction exactly like CounterboreFeature's/HoleFeature's own fields
// (axis pointing INTO the material). `steps` lists each segment's own
// radius/length/face_index in entry-to-far order: `steps[0].length` is
// measured from `origin` itself, and each subsequent step's own `length`
// is measured from where the PREVIOUS step ends, not as a running total
// from `origin` - there is no fixed "two steps" bound here to give a
// single obvious second running-total field a name the way
// CounterboreFeature's own `drill_depth` (a total-from-entry convention)
// has, so a caller wanting a running total sums the steps up to and
// including the one it wants. `through` reflects the chain's own far end,
// exactly like HoleFeature::through/CounterboreFeature::through.
//
// Unlike RecognizeCounterboreHoles() itself, this does NOT require the
// chain's own radii to trend in any particular direction (narrowing
// monotonically toward the far end, the way a real counterbore's own
// wide-then-narrow convention always does) - any sequence of adjacent,
// non-overlapping, pairwise-different-radius cylindrical segments on the
// same axis line merges into one chain here, including one that widens
// then narrows again (a spot-face recess wider than the counterbore
// beneath it, say) - a deliberately more general match than
// CounterboreFeature's own fixed convention, verified directly for a
// non-monotonic case (see this function's own test).
struct SteppedHoleChain {
  Point3d origin;
  Vector3d axis;
  std::vector<SteppedHoleStep> steps;
  bool through = false;
};

// Scans every face of `solid` the same way RecognizeCounterboreHoles()
// does (CONCAVE candidates only), but instead of matching only the
// first-found adjacent pair, walks each candidate's own chain of
// adjacent, non-overlapping, same-axis-line, pairwise-different-radius
// neighbors as far as it goes via this file's own internal
// FindSteppedChains() helper (the generalization of
// FindAdjacentSteppedPairs() from exactly two segments to an arbitrary
// chain), then reports every chain of THREE OR MORE segments as one
// SteppedHoleChain - a plain single-radius hole (RecognizeHoles()'s own
// domain) and an exactly-two-segment counterbore (RecognizeCounterboreHoles()'s
// own domain) are both left alone here, not repeated: this function's
// own output is disjoint from both of theirs, the same one-capability-
// several-vocabularies convention this file's other Recognize* functions
// already follow (see e.g. CounterboreFeature's own doc comment).
//
// The chain's own two outer ends are classified open/capped with the
// exact same on-axis Mesh::ContainsPoint() test RecognizeHoles()/
// RecognizeCounterboreHoles() themselves use, tessellating `solid` once
// and reusing it for every candidate chain; a chain with neither end open
// (entirely enclosed) is silently skipped, mirroring RecognizeHoles()'s
// own "both ends capped" skip.
//
// Still partial: only the CYLINDRICAL-step case is recognized, same as
// RecognizeCounterboreHoles() itself - a countersink's own conical step
// is a different surface type and never joins a chain here; a general
// (non-cylindrical) pocket remains out of scope; and, like every other
// Recognize* function in this file, this inherits Mesh::ContainsPoint()'s
// own disclosed "closed, consistently-oriented mesh" precondition.
std::vector<SteppedHoleChain> RecognizeSteppedHoleChains(const Brep& solid);

// One compound stepped/shouldered boss recognized on an existing solid
// (parity-map "Feature recognition" - closes BossFeature's own disclosed
// "a counterbore/countersink's own second step ... has no boss-side
// analogue implemented here" gap): the boss-side mirror of
// CounterboreFeature/RecognizeCounterboreHoles() above - two coaxial
// CONVEX full-cylinder faces over ADJACENT, non-overlapping axial ranges,
// with different radii, reported as ONE feature instead of
// RecognizeBosses()'s own two independent BossFeature entries for the
// same shape (e.g. a bolt-style boss with a wide shoulder/flange at its
// own base and a narrower shaft continuing on to the tip, or the
// less-common opposite - a narrow post rising from a wide pad at its
// free end).
//
// Unlike a counterbore's fixed "wide step always sits at the entry"
// convention, a stepped boss has no such fixed rule - EITHER of its own
// two segments can be the one actually attached to the body it emerges
// from, so `base_radius`/`base_height` always describe whichever segment
// is genuinely attached (`origin` sits at its own outer, attached end),
// and `tip_radius`/`tip_height` the other, regardless of which one is
// wider. `axis` is a unit vector pointing AWAY from the material, from
// `origin` toward the free tip - BossFeature's own convention, not
// HoleFeature's. `through` mirrors BossFeature::through: true only when
// NEITHER segment's own outer end is attached (a free-standing stepped
// rod, `origin`/`axis` pinned to one end arbitrarily - whichever segment
// the internal pairing scan happens to visit first, the same "pin one
// end, no particular meaning to which" convention BossFeature's own
// through case already uses) - NOT a chain embedded
// partway through a wall and protruding both sides, for the same reason
// BossFeature's own doc comment gives.
struct SteppedBossFeature {
  Point3d origin;
  Vector3d axis;
  double base_radius = 0.0;
  double base_height = 0.0;
  double tip_radius = 0.0;
  double tip_height = 0.0;
  bool through = false;

  // Face indices of the two walls this feature was merged from - the
  // segment at `origin` (the base) and the other (the tip), in that
  // order. Same convention as CounterboreFeature's own
  // counterbore_face_index/drill_face_index.
  int base_face_index = -1;
  int tip_face_index = -1;
};

// Scans every face of `solid` the same way RecognizeBosses() does
// (CONVEX full cylinders only), but instead of reporting every one as its
// own independent boss, looks for PAIRS sharing the same axis LINE with
// adjacent, non-overlapping axial ranges and genuinely different radii -
// the exact geometric match RecognizeCounterboreHoles() itself looks for
// among CONCAVE candidates, shared via this file's own internal
// FindAdjacentSteppedPairs() helper. A candidate found with BOTH of its
// own two outer ends attached (the whole two-segment chain entirely
// embedded in `solid`'s own bulk, exposed nowhere) is not a visible
// feature and is silently skipped, mirroring RecognizeBosses()'s own
// "both ends attached" skip for a single segment.
//
// Still partial, for the same reasons RecognizeCounterboreHoles() itself
// discloses: a stepped chain of more than two radii is not walked past
// the first adjacent pair, and this inherits Mesh::ContainsPoint()'s own
// disclosed "closed, consistently-oriented mesh" precondition - including
// RecognizeBosses()'s own CONFIRMED Union-side gap (a boss whose own base
// cap was left backed off entirely inside the target by
// BooleanCombineGeneral() misclassifies across its whole embedded span;
// see BossFeature's own doc comment) for a stepped boss built that way.
std::vector<SteppedBossFeature> RecognizeSteppedBosses(const Brep& solid);

// One segment of a multi-step chain recognized by
// RecognizeSteppedBossChains() below - the boss-side mirror of
// SteppedHoleStep, in base-to-tip order within the enclosing
// SteppedBossChain.
struct SteppedBossStep {
  double radius = 0.0;
  double height = 0.0;
  int face_index = -1;
};

// A compound boss with THREE OR MORE coaxial cylindrical steps - the
// boss-side mirror of SteppedHoleChain above, closing
// RecognizeSteppedBosses()'s own disclosed "a stepped chain of more than
// two radii ... is not walked past the first adjacent pair" gap for a
// convex chain (e.g. a flanged boss whose shaft itself steps down to a
// narrower threaded stub, or any other chain of adjacent same-axis convex
// cylindrical steps).
//
// `origin`/`axis` describe the chain's own base (attached) end and
// outward direction exactly like SteppedBossFeature's/BossFeature's own
// fields (axis pointing AWAY from the material, base toward the free
// tip). `steps` lists each segment's own radius/height/face_index in
// base-to-tip order - the same "each entry measured from where the
// previous one ends, not a running total from origin" convention
// SteppedHoleChain's own `steps` field uses. Like a stepped boss's own
// two-segment case (SteppedBossFeature), and unlike a counterbore's fixed
// "wide is always the entry side" rule, a chain here has NO fixed
// "widest/narrowest segment is always the base" convention either - EITHER
// end of the chain can be the one genuinely attached, and this reports
// whichever one actually is (via the same on-axis Mesh::ContainsPoint()
// attached/free probe SteppedBossFeature's own construction already
// uses), regardless of that end's own radius. `through` mirrors
// SteppedBossFeature::through: true only when NEITHER outer end is
// attached (a free-standing multi-step rod).
struct SteppedBossChain {
  Point3d origin;
  Vector3d axis;
  std::vector<SteppedBossStep> steps;
  bool through = false;
};

// Scans every face of `solid` the same way RecognizeSteppedBosses() does
// (CONVEX candidates only), but instead of matching only the first-found
// adjacent pair, walks each candidate's own chain of adjacent,
// non-overlapping, same-axis-line, pairwise-different-radius neighbors as
// far as it goes via FindSteppedChains() (shared with
// RecognizeSteppedHoleChains() above - the same "share the chain-walking
// geometry, not a second copy of the loop" precedent
// FindAdjacentSteppedPairs() itself already set for
// RecognizeCounterboreHoles()/RecognizeSteppedBosses()), then reports
// every chain of THREE OR MORE segments as one SteppedBossChain - a plain
// single-radius boss and an exactly-two-segment stepped boss
// (RecognizeSteppedBosses()'s own domain) are both left alone here, not
// repeated.
//
// A candidate chain found with BOTH of its own two outer ends attached
// (the whole chain entirely embedded in `solid`'s own bulk, exposed
// nowhere) is not a visible feature and is silently skipped, mirroring
// RecognizeSteppedBosses()'s own "both ends attached" skip for a
// two-segment pair.
//
// Still partial, for the same reasons RecognizeSteppedBosses() itself
// discloses: this inherits Mesh::ContainsPoint()'s own disclosed "closed,
// consistently-oriented mesh" precondition, including RecognizeBosses()'s
// own CONFIRMED Union-side gap (see BossFeature's own doc comment) for a
// stepped-boss chain built via BooleanCombineGeneral()'s own Union path
// specifically; and no `dino8-app` command surfaces any of this.
std::vector<SteppedBossChain> RecognizeSteppedBossChains(const Brep& solid);

}  // namespace dino8::kernel
