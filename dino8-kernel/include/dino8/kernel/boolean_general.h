// General boundary-evaluation boolean engine.
//
// Unlike boolean.cpp's BooleanCombineMixed (which is hand-solved,
// closed-form geometry enumerated per surface-TYPE-pair: plane+plane,
// plane+cylinder, cylinder+cylinder, ...), BooleanCombineGeneral works on
// ANY pair of ON_Surface-based faces by actually running the general
// surface/surface intersector (dino8/kernel/surface_intersect.h) between
// every (bbox-overlapping) face pair, splitting each face's own trim loop
// along the resulting intersection curves, classifying each fragment
// in/out of the other solid by ray-casting (via the general curve/surface
// intersector, not hand-solved ray-vs-plane/ray-vs-cylinder formulas), and
// reassembling the kept fragments into a genuine ON_Brep (real
// ON_BrepVertex/ON_BrepEdge/ON_BrepLoop/ON_BrepTrim topology, coincident
// points welded into shared vertices/edges - the same identity mechanism
// Brep::FromMixedFaces() already uses, just generalized to an arbitrary
// ON_Surface instead of only a plane/cylinder/cone).
//
// Scope/limitations (see boolean_general.cpp's own top comment for the
// full disclosure): a face is expected to carry at most one "outer"
// intersection component per opposing face pair that either (a) closes
// entirely inside the face's own trim (a closed loop - becomes a hole in
// the untouched fragment plus a separate interior fragment), or (b) meets
// the face's own trim boundary at exactly two points (an open arc - splits
// that trim boundary into two fragments there). Faces are assumed genus-0
// (no pre-existing holes) two-shell solids. Multiple non-interacting
// chains on the same face are supported (each is spliced in turn); chains
// that cross EACH OTHER on the same face are not.
#pragma once

#include <utility>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

// `tolerance` is the SSX Newton-refinement accuracy (dino8/kernel/
// surface_intersect.h's own IntersectOptions::tolerance - the max |S1 - S2|
// every intersection-curve point is polished to before it becomes a
// polyline edge in the result) - the parity-map "Tolerant booleans
// (caller-specified tolerance)" item: previously every entry point in this
// file default-constructed its own IntersectOptions with no way for a
// caller to loosen or tighten it. Defaults to IntersectOptions's own
// default (0.001), reproducing this function's exact prior behavior for
// every existing caller that doesn't pass one. Tightening it measurably
// improves how closely a curved result's own edges (dense polylines, see
// this file's own top comment) sit on the true analytic intersection - it
// does NOT gap-heal imprecise operands or change this engine's own other
// disclosed scope limits (one crossing chain per face pair, genus-0
// faces).
//
// `Union` still refuses either operand being a `Brep::Compound()` of two or
// more lumps (e.g. a SymmetricDifference result) with std::invalid_argument -
// compound lumps that touch or overlap have no single manifold shell for
// this engine's per-face ray-cast classification to build, the same reason
// boolean.cpp's own BooleanCombinePlanar/BooleanCombineMixed refuse one for
// Union too (see boolean_general.cpp's own RefuseCompoundOperand doc
// comment). `SymmetricDifference` is refused outright regardless of either
// operand (see below). `Difference`/`Intersection` accept a compound operand
// on either side - they distribute over a compound operand's lumps exactly,
// and this engine's classification (a ClassifyPointVsBrep ray-cast against
// the OTHER operand's full face list, or the coincident-face override's own
// per-face normal comparison) already has no notion of which lump a face
// came from, so it classifies a multi-lump operand exactly as correctly as
// a single-lump one - the identical reasoning boolean.cpp's own two B-rep
// engines already rely on for their own identical exemption; confirmed
// directly, not merely assumed, by tessellating the resulting SHAPE as a
// whole via TessellateGeneralBooleanClosedMesh() and getting a genuinely
// closed manifold with the exact right combined volume (see
// TestBooleanCombineGeneralDifference/IntersectionAcceptsCompoundOperand*,
// tests/test_basic.cpp).
//
// Unlike BooleanCombinePlanar/BooleanCombineMixed (boolean.cpp), this
// function does NOT recompute the result's true lump structure via
// SplitDisjointPieces()/Brep::Compound() the way those two do - tried
// directly while building this, not skipped out of caution:
// SplitDisjointPieces()'s own ON_Brep::DuplicateFaces() step corrupts
// whatever TessellateGeneralBooleanClosedMesh()'s own T-junction stitching
// needs from this engine's own dense-polyline trim edges, so a result that
// tessellates perfectly (closed, correct volume) as ONE unsplit Brep
// tessellates WRONG (open, wrong volume) once split into pieces and
// Compound()-ed back together - a regression the split introduces, not a
// pre-existing defect it merely exposes. So a compound-input result from
// this function always reports `LumpFaceRanges().size() == 1` - a real,
// disclosed bookkeeping gap PARITY_MAP.md's "Multi-body / multi-tool
// booleans" bullet leaves open for this one engine, not a hint that the
// geometry itself is wrong (it isn't - see the closed/volume confirmation
// above). This engine's own output is never itself a Brep::Compound() by
// construction, so chaining two BooleanCombineGeneral calls - including
// every pairwise step BooleanCombineGeneralNAry makes - sees an ordinary
// single-lump-reporting Brep on the way back in, unaffected by whether the
// call that produced it took a compound operand.
Brep BooleanCombineGeneral(const Brep& a, const Brep& b, BooleanOp op, double tolerance = 0.001);

// N-ary counterpart of BooleanCombineGeneral - third and last of this
// category's three B-rep boolean engines to get one (see
// dino8/kernel/boolean.h's BooleanCombineMixedNAry for the full rationale
// shared by all three: PARITY_MAP.md's "Multi-body / multi-tool booleans"
// bullet's "No kernel N-ary API" gap). Same fold shape: `first_group` is
// folded left-to-right into one solid via repeated
// BooleanCombineGeneral(..., Union, tolerance) calls; if `second_group` is
// non-empty it is folded the same way and the two folded solids are
// combined via one further BooleanCombineGeneral(..., op, tolerance) call,
// otherwise the folded `first_group` is returned directly (and `op` must
// be Union). SymmetricDifference is refused - BooleanCombineGeneral()
// itself does not implement it at all (see that function's own doc
// comment above), so there is nothing for an N-ary fold to build on.
// `tolerance` is forwarded unchanged to every pairwise call this function
// makes. Like BooleanCombineMixedNAry/BooleanCombinePlanarNAry, this
// function refuses a compound (multi-lump) operand at every pairwise fold
// step - BooleanCombineGeneral() itself now has its own file-local
// RefuseCompoundOperand guard (boolean_general.cpp), closing the
// "silently unguarded... by the general engine" gap the "Multi-body /
// multi-tool booleans" bullet used to name.
Brep BooleanCombineGeneralNAry(const std::vector<Brep>& first_group, const std::vector<Brep>& second_group,
                                BooleanOp op, double tolerance = 0.001);

// Face-face imprint (Parasolid PK_BODY_imprint / ACIS imprint): splits
// `target`'s own faces wherever they cross `tool`'s faces, WITHOUT removing
// any material - every fragment of every `target` face is kept, unlike
// BooleanCombineGeneral() above, which ray-casts each fragment in/out of the
// other operand and drops half of them. `tool` is read-only and untouched -
// only `target`'s own topology changes (the same overall shape, as more,
// smaller faces along the same exact boundary). Reuses this file's own
// SSX-driven face-fragmentation machinery (IntersectFaces() + FragmentFaces()
// in boolean_general.cpp - the same helpers BooleanCombineGeneral() itself
// calls before its own classification step), so it inherits that machinery's
// own disclosed scope limits (see this file's own top-of-file doc comment:
// at most one "outer" intersection chain per opposing face pair, genus-0
// operand faces, non-self-crossing chains on one face). Either operand may
// be open (a sheet) or closed (a solid) - imprint never ray-casts against
// either one, so it has none of BooleanCombineGeneral()'s own closed-solid
// requirement.
//
// `tolerance` is the same caller-controlled SSX Newton-refinement accuracy
// BooleanCombineGeneral() above now takes (dino8/kernel/surface_intersect.h's
// own IntersectOptions::tolerance) - the parity-map "Tolerant booleans
// (caller-specified tolerance)" item previously called out this function by
// name as one of the entry points still hardcoding its own tolerance with no
// caller control at all. Defaults to IntersectOptions's own default (0.001),
// reproducing this function's exact prior behavior for every existing
// caller that doesn't pass one.
//
// Throws std::invalid_argument if `target` or `tool` has no faces at all -
// there is nothing to imprint on/with, the same typed-refusal convention
// BooleanCombineGeneral() itself uses for an out-of-scope call - or if
// `tolerance` is not positive. Returns an empty Brep (not an error) if
// `target`'s faces all failed to reach a valid (>= 3 point) boundary loop
// after fragmentation, mirroring BooleanCombineGeneral()'s own
// "kept.empty()" convention.
Brep ImprintFaces(const Brep& target, const Brep& tool, double tolerance = 0.001);

// Mutual imprint: the natural extension of ImprintFaces() above to BOTH
// operands at once - splits `a`'s own faces wherever they cross `b`, AND
// `b`'s own faces wherever they cross `a`, with neither ever losing
// material, matching Parasolid/ACIS's own two-way imprint (as opposed to
// ImprintFaces()'s own read-only `tool`). Implemented exactly as the
// parity map itself already describes this gap's own closing move: call
// ImprintFaces() twice, with the operands swapped the second time -
// `ImprintFaces(a, b, tolerance)` for the first half, `ImprintFaces(b, a,
// tolerance)` for the second - since each call already only ever mutates
// its own `target` and treats `tool` as read-only, so nothing about
// running both is unsound; there is no cross-call state to reconcile.
//
// Returns {a_imprinted, b_imprinted} - either may equal a copy of the
// original operand's own shape (more, smaller faces along an unchanged
// overall boundary) if the OTHER operand alone crosses it; both may if
// `a`/`b` don't intersect at all. Same throws as ImprintFaces() itself,
// checked against BOTH operands before either call runs (so a bad `b`
// refuses before `a` is ever imprinted, not after doing half the work).
std::pair<Brep, Brep> MutualImprintFaces(const Brep& a, const Brep& b, double tolerance = 0.001);

// Split a single face along a curve (parity-map "Split face by curve /
// surface (real trim-loop split in place)" - localops category). Unlike
// dino8-app's own `SplitFaceCommand` (cmd_fillet.cpp:2057), which only
// finds where `curve` crosses `target`'s face_index'th face and splits the
// underlying SURFACE at the iso-parameter MIDPOINT of those hits (an
// approximation that ignores the curve's actual shape between crossings),
// this performs a genuine trim-loop split: `curve` is pulled onto the
// face's own surface (each sample's closest point, via this file's own
// SurfaceClosestPointGlobal() - the real multistart-Newton solver
// surface.cpp's NurbsSurface::ClosestPointParameter() already wraps, called
// here directly on the face's raw ON_Surface), turned into a dense (u, v)
// polyline chain, and spliced into the face's own trim-loop boundary via
// this file's own FragmentFaces()/SplitFaceLoop() - the identical machinery
// ImprintFaces() above uses for a whole tool BODY, just fed one caller-
// supplied curve chain for one named face instead of a set of SSX curves
// gathered from a second operand. No new geometry is fit for either half's
// own trim curve: exactly like every other Fragment this file produces,
// each new face keeps the ORIGINAL surface, unchanged, with a genuinely
// new trim boundary running along the real (sampled) curve rather than a
// straight chord between its two crossing points.
//
// `face_index` must be in range (`target` must have that many faces at
// all) or this throws std::invalid_argument, alongside a non-positive
// `tolerance` or a `curve` with fewer than 2 control points. An UNTRIMMED
// face (e.g. one of `Brep::Box()`'s own six faces) is a valid target too -
// the same FaceBoundaryLoop() this file's other operations already share
// falls back to the surface's own full parameter-domain rectangle as its
// boundary when there is no real ON_BrepLoop, so the curve splices against
// that. Throws std::invalid_argument if
// `curve`, once pulled onto the face's surface, does not split that face's
// own trim loop into EXACTLY two fragments - e.g. it never reaches the
// face's own boundary at both ends (an interior-only touch becomes a hole,
// not a split), or it crosses the boundary more than twice - since neither
// case is the "one curve, two resulting faces" operation this function
// promises; a caller after a partial/best-effort split should reach for
// ImprintFaces() instead, which keeps every fragment unconditionally.
// Every OTHER face of `target` is carried through unchanged (its own
// single, untouched fragment), the same convention ImprintFaces() uses for
// a `target` face that no SSX curve ever reaches.
//
// `samples` controls how finely `curve` is discretized before being pulled
// onto the surface - the same "polyline stands in for the true curve, to
// within a caller-tunable resolution" contract `NurbsCurve::Length()` and
// `ClosestPointParameter()` already use elsewhere in this kernel, not a
// hidden approximation unique to this function.
Brep SplitFaceByCurve(const Brep& target, int face_index, const NurbsCurve& curve, double tolerance = 0.001,
                      int samples = 200);

// Sheet/solid trim (parity-map "Sheet/solid trim (open surface as cutter
// through a solid)"): splits `solid` (a closed Brep) into the two pieces
// on either side of `sheet` (an OPEN Brep - one or more trimmed faces used
// purely as a cutting tool, NOT required to enclose a volume the way
// BooleanCombineGeneral()'s own operands must), each piece capped with the
// portion of `sheet` that lies inside `solid`. The pair-returning sibling
// of boolean.cpp's own `SplitByPlane` (mesh half-space split), for the
// same reason: which piece is "kept" is a caller/UI decision, not a
// geometric one, so both come back rather than one being silently
// discarded.
//
// Reuses this file's own SSX-gathering + FragmentFaces() machinery for
// BOTH operands (see this file's own top-of-file doc comment for the
// scope that implies), but classifies them two different ways: `solid`'s
// own fragments are bucketed by which side of `sheet` they fall on (a
// closest-point-plus-normal-sign test, not ray-cast parity - `sheet` may
// have no volume to be in/out of); `sheet`'s own fragments are ray-cast
// in/out of `solid` as usual (valid because `solid`, unlike `sheet`,
// really is closed), and only the IN ones become a new cap face, one
// oriented copy added to each output piece.
//
// Requires `sheet` to fully sever `solid` (a cutting surface extending
// past `solid`'s own silhouette) for every one of `solid`'s own fragments
// to classify; throws std::invalid_argument if either operand has no
// faces, or if `sheet` is degenerate (no face of it converges a closest
// point for some fragment of `solid`), or if `tolerance` is not positive.
// Returns {positive_side, negative_side} - `positive_side` is the piece on
// the side each nearest `sheet` face's own outward normal (m_bRev-corrected)
// points into; either may come back the empty Brep if `sheet` doesn't
// actually cross `solid` at all, mirroring ImprintFaces()'s own
// "kept.empty()" convention rather than treating a clean miss as an error.
//
// `tolerance` is the same caller-controlled SSX Newton-refinement accuracy
// BooleanCombineGeneral()/ImprintFaces() above take - previously named
// alongside ImprintFaces() as still hardcoding its own tolerance with no
// caller control. Defaults to IntersectOptions's own default (0.001),
// reproducing this function's exact prior behavior for every existing
// caller.
std::pair<Brep, Brep> SplitBySheet(const Brep& solid, const Brep& sheet, double tolerance = 0.001);

// The OTHER half of the "Sheet/solid trim" parity-map item SplitBySheet()
// above leaves undone: trimming a SHEET's own surface down BY a solid,
// rather than splitting a solid by a sheet. `sheet` (an open Brep, same
// sense as SplitBySheet's own `sheet` operand) is never modified or
// re-capped - `solid` (a closed Brep) is used purely as the ray-cast-
// classification target for `sheet`'s own SSX fragments, via the same
// ClassifyPointVsBrep() this file's other operations already use.
//
// Returns the portion of `sheet` that lies inside `solid` when
// `keep_inside` is true (the default - Rhino's Trim/Split convention of
// discarding the part the user clicked away), or the portion outside when
// false. `solid` itself is never split, capped, or returned - unlike
// SplitBySheet(), which hands back both of `solid`'s own halves.
//
// Reuses this file's own SSX-gathering + FragmentFaces() machinery (see
// this file's own top-of-file doc comment for the scope that implies: at
// most one "outer" intersection chain per opposing face pair, genus-0
// operand faces, non-self-crossing chains on one face).
//
// Throws std::invalid_argument if either operand has no faces, or if
// `tolerance` is not positive. Returns the empty Brep (not an error) if
// none of `sheet`'s own fragments fall on the requested side - either
// because `sheet` never reaches `solid` at all, or because it lies
// entirely on the other side - mirroring SplitBySheet()'s own
// "kept.empty()" convention rather than treating a clean miss as an
// error.
//
// `tolerance` is the same caller-controlled SSX Newton-refinement accuracy
// BooleanCombineGeneral()/ImprintFaces()/SplitBySheet() above take.
// Defaults to IntersectOptions's own default (0.001), reproducing this
// function's exact prior behavior for every existing caller.
Brep TrimSheetBySolid(const Brep& sheet, const Brep& solid, bool keep_inside = true, double tolerance = 0.001);

// A blind or through round hole (Rhino/SolidWorks "Hole" feature), cut
// straight into `solid` via BooleanCombineGeneral() above - so, unlike the
// app's `RoundHole`/`MakeHole`/`PlaceHole` (dino8-app/src/commands/
// cmd_solidtools.cpp, all still "mesh boolean; results are meshes"), the
// result is a genuine ON_Brep. `center` is the hole's entry point (where
// its axis meets the surface being drilled) and `axis` the drilling
// direction, pointing INTO the material (need not be unit length -
// normalized internally). The cutting tool is a plain capped cylinder
// (Brep::Pipe() over a straight two-point rail - the exact rational
// cylinder that degree-1/2-station case already gives), backed off
// `center` by a small margin along `-axis` so it pierces the entry
// surface cleanly rather than merely grazing it tangentially (the same
// "extend the cutter past the target's own silhouette" convention
// dino8-app's own SolidifyOpenCutter already uses for its open-surface
// cutters).
//
// `through`: when true, the tool is extended far enough past `solid`'s
// own tight bounding box (twice its diagonal) to guarantee it exits the
// far side regardless of `solid`'s shape, and `depth` is ignored. When
// false, the hole is blind: the tool's far end sits at
// `center + unit(axis) * depth` and is capped there, giving the hole a
// flat bottom (Pipe()'s own auto-cap, not a drill-point taper) - `depth`
// must then be positive.
//
// Throws std::invalid_argument for a non-positive `radius`, a
// non-positive `depth` on a blind hole, a zero-length `axis`, or a
// `solid` with no faces at all.
Brep MakeHole(const Brep& solid, Point3d center, Vector3d axis, double radius, double depth, bool through = false);

// Counterbore hole (Rhino/SolidWorks "Counterbore Hole" feature,
// parity-map "Counterbore (stepped coaxial) hole"): MakeHole()'s own
// straight bore, plus a larger-diameter, shallower coaxial recess at the
// entry surface for a bolt head/nut to sit flush. Built as ONE exact
// stepped-profile Brep::Revolve() call (the same "profile touches the
// axis at one end, off-axis at the other -> auto-capped solid of
// revolution" construction Brep::Revolve()'s own doc comment already
// establishes for a plain cylinder/cone/frustum, extended here to a
// two-step profile) rather than two separate cylinders unioned together,
// so only ONE BooleanCombineGeneral() call is ever made against `solid` -
// avoiding this engine's own disclosed "faces assumed genus-0, no
// pre-existing holes" scope limit that a second, chained Difference
// against an already-holed `solid` would otherwise risk (see this file's
// own top-of-file doc comment).
//
// `counterbore_radius` must exceed `bore_radius`, and `counterbore_depth`
// must be positive (the counterbore recess is always blind, even when the
// bore itself is `bore_through`). `bore_depth`/`bore_through` behave
// exactly as MakeHole()'s own `depth`/`through`.
Brep MakeCounterboreHole(const Brep& solid, Point3d center, Vector3d axis, double bore_radius, double bore_depth,
                          bool bore_through, double counterbore_radius, double counterbore_depth);

// Countersink hole (Rhino/SolidWorks "Countersink Hole" feature,
// parity-map "Countersink (conical) hole"): MakeHole()'s own straight
// bore, plus a conical flare at the entry surface for a flat-head screw,
// sized by `countersink_diameter` (the cone's own diameter AT the entry
// surface - must exceed 2*bore_radius) and `countersink_angle_degrees`
// (the cone's full included angle, e.g. the standard 82/90/100/120 degree
// countersinks - must be in (0, 180)). The countersink's own depth (the
// surface-to-bore-radius transition) is derived from that angle and the
// radius gap rather than taken as a separate parameter, matching how a
// real countersink cutting tool is specified. Built as one exact
// stepped-profile Brep::Revolve() call (a flat mouth disc, then a conical
// frustum wall down to `bore_radius`, then the straight bore - the exact
// frustum construction Brep::Revolve()'s own doc comment already
// establishes) and one BooleanCombineGeneral() Difference call, for the
// same "avoid a second boolean call against an already-holed operand"
// reason MakeCounterboreHole() above gives.
Brep MakeCountersinkHole(const Brep& solid, Point3d center, Vector3d axis, double bore_radius, double bore_depth,
                          bool bore_through, double countersink_diameter, double countersink_angle_degrees);

// Emboss (raise) or deboss (engrave) a closed planar profile onto `solid`
// (parity-map "kernel: Feature operations" - "Emboss/deboss", previously
// zero hits for emboss/deboss/engrave anywhere in the codebase). Not a new
// boolean engine: `profile` is turned into a real capped solid tool via
// Brep::Extrude() (brep.h - so `profile` inherits that function's own
// "closed, planar, star-shaped" capping requirement, and throws whatever
// Extrude() itself throws for a profile that doesn't satisfy it), then
// combined with `solid` via ONE BooleanCombineGeneral() call - Union for
// EmbossMode::Emboss (fuses a raised boss onto the surface), Difference for
// EmbossMode::Deboss (cuts an engraved pocket into it).
//
// `direction` points INTO the material - the same convention MakeHole()'s
// own `axis` uses - and need not be perpendicular to `profile`'s own plane
// (an oblique emboss/deboss is fine, exactly as Extrude() itself allows),
// just not lie IN it. `depth` is how far the boss protrudes (Emboss) or the
// pocket cuts in (Deboss), measured from `profile`'s own plane.
//
// Mirrors MakeHole()'s own "back the tool off by a small margin so it
// crosses the target's surface transversally rather than grazing it at a
// numerically degenerate coincident touch" convention, applied to whichever
// side of `profile`'s own plane actually needs it: Deboss's tool starts
// `margin` in FRONT of the plane (outside the material, so its own entry
// cap pierces the surface cleanly) and cuts `depth` past it; Emboss's tool
// starts `margin` BEHIND the plane (embedded `margin` deep in the material,
// so the Union has real volume to fuse onto rather than a tangent touch)
// and protrudes `depth` past it the other way.
//
// Throws std::invalid_argument if `solid` has no faces, if `profile` is not
// a closed curve, if `depth` is not strictly positive, or if `direction` is
// zero-length - plus whatever Brep::Extrude() itself throws for a `profile`
// that isn't planar, isn't star-shaped (a self-crossing or reflex outline
// can't be fanned into a flat cap), or whose plane contains `direction`.
enum class EmbossMode { Emboss, Deboss };
Brep EmbossProfile(const Brep& solid, const NurbsCurve& profile, Vector3d direction, double depth, EmbossMode mode);

// Extrude a closed planar QUADRILATERAL profile "to a boundary"
// (PARITY_MAP.md's "kernel: Sweeping, lofting, extruding, revolving" gap
// - "Extrude to a boundary surface / body (Rhino ToBoundary,
// Boss-to-boundary; AutoCAD extrude 'to face', PressPull)"): each of
// `profile`'s own 4 corners is swept along `direction` and intersected
// exactly with `boundary`'s own plane (a plain ray/plane intersection,
// closed form - not a resample), giving a genuinely, exactly planar cap
// even when `boundary` is tilted relative to `direction`; the result is
// assembled as a real 6-quad-face prism via `Brep::FromUntrimmedQuadFaces()`
// (brep.h): 2 caps (the original footprint and the new exact cap) plus 4
// side walls.
//
// An earlier version tried to reuse SplitBySheet() above instead (extrude
// `profile` far past `boundary`, then cut the oversized solid against it,
// so a general, not-necessarily-planar `boundary` would have been in
// scope) - abandoned after being confirmed, via a standalone reproduction,
// to corrupt SplitBySheet's own output for anything but a plain
// axis-aligned `Brep::Box()`: `boolean_general.cpp`'s SSX machinery reads
// each operand face purely via its raw `ON_Surface`, with no awareness of
// this kernel's own separate polygon-trim side table, so both
// `Brep::Extrude()`'s own periodic wrap-around wall and
// `Brep::FromPlanarFaces()`'s own genuinely-trimmed-but-padded-domain
// faces are silently misread as occupying their own FULL surface domain,
// not their true (smaller) shape. Rather than debug that shared, heavily
// depended-on machinery under this feature's own scope, this closed-form
// per-corner construction sidesteps it entirely - at the cost of two real
// narrowings, both disclosed rather than silently assumed away:
//   - `profile` must be a quadrilateral (exactly 4 vertices, closed,
//     planar, degree-1, non-rational) - `Brep::FromUntrimmedQuadFaces()`'s
//     own "surface domain IS the whole true shape" contract (the same one
//     `Brep::Box()` itself relies on) only holds for a genuine quad face;
//     an N-gon cap for N != 4 would need real trim topology to represent
//     honestly, exactly the thing just ruled out above.
//   - `boundary` must be a single planar face (checked via
//     `Brep::PlanarFaces()`, which also hands back the fitted `ON_Plane`
//     directly) - a general curved or multi-face boundary is out of
//     scope; unlike the abandoned SplitBySheet approach, this never finds
//     an intersection CURVE at all, only a plane equation.
//
// `boundary`'s own FINITE extent is still respected, not just its
// infinite plane: each computed cap corner is projected into `boundary`'s
// own local (x, y) and checked against its real polygon (`PointInPolygon`,
// surface_intersect.h) - a `boundary` positioned so far to the side that
// it never actually reaches `profile`'s own swept footprint is refused,
// not silently capped against empty space.
//
// Throws std::invalid_argument if `profile` isn't a closed, planar,
// degree-1, non-rational quadrilateral; if `direction` is zero, non-finite,
// or lies in `profile`'s own plane; if `boundary` doesn't resolve to
// exactly one planar face (`PlanarFaces()`'s own precondition, or this
// function's own single-face requirement); if `direction` is parallel to
// `boundary`'s own plane (never reached, at any distance); if `boundary`
// lies behind `profile` along `direction` for at least one corner (only
// reachable extruding the other way, which this never guesses); or if at
// least one corner's exact crossing point falls outside `boundary`'s own
// finite extent (its plane is reached, but not within the surface that
// actually occupies it there).
Brep ExtrudeToBoundary(const NurbsCurve& profile, Vector3d direction, const Brep& boundary);

// A purely additive, opt-in sibling of Brep::TessellateToClosedMesh()/
// TessellateToClosedMeshConforming(), scoped ONLY to BooleanCombineGeneral's
// own results, that closes the mesh-watertightness gap this file's own
// top-of-file doc comment discloses: `result`'s own faces are tessellated
// via Brep::Tessellate(u_divisions, v_divisions) - the exact same shared,
// unmodified grid-clip tessellator BooleanCombineMixed also depends on, not
// touched by this function at all - then, before welding, every boundary
// edge of one face's own tessellation that another face's tessellation
// happens to have an extra, un-partnered vertex sitting exactly on (this
// engine's own dense straight-segment polyline edges - see this file's own
// top comment - get resampled at different densities by the two adjacent
// faces' independent (u, v) grids, since neither Tessellate() nor
// TessellateConforming() has ever matched a general trimmed face's own
// polyline boundary the way TessellateConforming()'s existing analytic-
// curve/plain-quad passes match theirs) is re-triangulated as a fan through
// that extra vertex, so the two sides' boundary vertex sets agree exactly
// before Mesh::MergeAndWeld() runs. Also drops any resulting zero-area
// (degenerate, repeated-vertex) triangle - a separate, pre-existing
// grid-clip artifact (near a surface's own singular point, e.g. a
// sphere's pole, but also - confirmed directly - at an ordinary planar
// trim corner where two cut boundaries meet) that otherwise leaves
// spurious zero-length "edges" behind. This drop runs BOTH before and
// after the T-junction stitching pass above, not only after: dropping a
// boundary-line sliver strands its two OTHER edges - which the sliver
// had "claimed" as internal, so the stitcher never offered them a
// cross-face partner - as fresh, unmatched boundary edges once the
// sliver disappears, unless it is gone before the stitcher ever sees it.
// tests/general_boolean_sweep.cpp's own 76-combination measurement
// (BooleanCombineGeneral over 19 primitive-pair cases x 4 ops, at
// u_divisions=32/v_divisions=128) went from 0/76 to 14/76 genuinely
// Mesh::IsClosedManifold() from this reordering alone - every
// axis-aligned-planar case (box+box, disjoint/touching/rotated box
// pairs) now closes at any division count tried (8x8 through 32x128).
// The other 62 all pair a curved face (cylinder/cone/sphere/torus)
// against another face along a curved or skew intersection: each side's
// own grid-clip tessellation approximates that shared curve with its own
// independently-sampled dense polyline (not shared sample points, unlike
// a straight box edge, where both sides' clip points genuinely coincide
// once the sliver above stops hiding them) - a materially larger gap.
//
// FOLLOW-UP SESSION: real-edge-topology-conforming reconciliation, the
// technique TessellateToClosedMeshConforming() already uses for
// BooleanCombineMixed/Planar, adapted to this engine (see
// ReconcileEdgeTopology() in boolean_general.cpp's own implementation
// comments for the full root-cause/fix writeup). Walks `result`'s own
// genuine ON_BrepEdge topology and, for every interior (two-face) edge,
// reconciles its two adjacent faces' raw tessellation boundaries to one
// shared, chord-snapped point set BEFORE the plain point-matching pass
// above (StitchTJunctionsOnce(), kept unmodified as its fallback). Went
// from 14/76 to 15/76 - box+box (second box rotated 30deg about z)
// Intersection newly closes, box+box's own axis-aligned Union/A-B/B-A and
// every previously-closing case stay closed and volume-correct. The
// dominant remaining curved-vs-curved gap (box+cylinder etc.) is now
// root-caused two levels deep, both still open:
//   (1) NurbsSurface::TessellateGridClippedExact() can silently DROP a
//       genuine trim-polygon vertex outright under certain grid-alignment
//       degeneracies (confirmed: a shared cut circle sitting exactly on a
//       v-grid line loses roughly half its polyline vertices on the
//       curved side's own raw tessellation, not merely mis-sampling it).
//       An experimental same-session fix (inserting the missing vertex by
//       splitting whichever existing boundary edge it lies on) measurably
//       helped (box+cylinder Union: 1327 -> 808 unmatched boundary edges)
//       and passed the FULL existing test suite with zero failures, but
//       added enough runtime cost (an O(this face's own vertex count)
//       repair scan, worst-case per failed edge) that it was reverted
//       rather than shipped without a confirmed, complete 76-case
//       re-measurement and a cheaper repair-lookup - a concrete, laid-out
//       next increment, not a dead end.
//
//       ROOT-CAUSED (a later session, surface.cpp): the drop is
//       ClipConvex's (Sutherland-Hodgman) own strict `>= 0.0` half-plane
//       test losing a coin-flip to ordinary floating-point noise, over
//       and over. A trim_polygon boundary that is genuinely straight in
//       (u, v) (the common case here: a curved face cut by a planar face,
//       so the cut is dead straight since v is literally height) still
//       arrives as MANY near-duplicate collinear vertices, not one clip
//       edge - BuildLoop() (boolean_general.cpp) resamples every original
//       chain segment at up to ~samples_per_edge points regardless of
//       curvature, and each point is only Newton-refined to the
//       intersecting surfaces' own convergence tolerance (confirmed
//       directly: up to ~1e-11 (u, v)-unit jitter between neighbors on
//       this exact case, box+cylinder Union's cylinder-wall trim). A grid
//       cell corner sitting exactly on that line then gets tested against
//       dozens of near-duplicate copies of essentially the same infinite
//       line in a row, each with its own independent jitter; about half
//       of those redundant tests land the point a hair on the wrong side
//       by pure noise, and ONE wrong verdict anywhere in the sequence
//       drops the point for good (Sutherland-Hodgman only ever narrows
//       the clipped result). Confirmed by direct reproduction: the
//       cylinder wall's own cut-boundary trim_polygon at u_divisions=8/
//       v_divisions=32 (box+cylinder Union) carries 104 vertices for what
//       is geometrically a 4-corner rectangle.
//
//       A first fix attempt loosened ClipConvex's own inside test to a
//       small (u, v)-distance tolerance instead - REJECTED after direct
//       measurement: it also papers over a genuinely different, and
//       genuinely degenerate, case (two DIFFERENT real boundaries, e.g. a
//       cut landing exactly on a face's own UNTOUCHED domain edge, not
//       redundant copies of the SAME boundary) by manufacturing a
//       sliver's worth of real extra area there, which showed up as new
//       NONMANIFOLD (not boundary) mesh edges and broke previously-exact,
//       purely-planar box+box Intersection/A-B/B-A (15/76 -> 12/76).
//       Shipped fix instead: SimplifyCollinearRuns(), a single O(trim
//       size) pass (once per TessellateGridClippedExact call, not per
//       grid cell) that collapses a run of consecutive trim_polygon
//       vertices collinear with their own immediate original neighbors
//       (within this file's existing kDuplicatePointEpsilon-scale (u, v)
//       floor) down to that run's own two endpoints - removing the
//       REDUNDANCY that causes the noise-driven coin flip, rather than
//       loosening the test itself, so it cannot manufacture new area at
//       an unrelated two-boundary coincidence. Confirmed: collapses that
//       same 104-vertex trim down to its true 4 corners; full 76-case
//       sweep and full ctest suite both green, zero regressions anywhere
//       (including the box+box cases the first attempt broke).
//
//       Measured impact is real but SMALL, not the hoped-for large one:
//       box+cylinder Union/Intersection/Difference's own naked-boundary-
//       edge count (TessellateGeneralBooleanClosedMesh, DiagnoseManifold
//       in scratch_test.cpp) each drop by ~1% (380->378, 234->231,
//       252->249); the sweep's own aggregate closedmesh count does not
//       move (still 15/76). Root-caused why, not just observed, by
//       instrumenting ReconcileEdgeTopology (boolean_general.cpp)
//       directly: every one of the 224 short polyline segments making up
//       box+cylinder Union's own z=-1 cut circle fails its WALL-side
//       match (`ok_b=0`) while its box-face side matches fine (`ok_a=1`).
//       The wall's own GridClippedExact tessellation only ever places
//       boundary vertices at u_divisions grid-corner resolution along
//       that straight cut (now, correctly, just as many as the geometry
//       needs - see the fix above); the box face's own boundary is built
//       from the ORIGINAL, much finer BuildLoop() polyline. Reconcile-
//       EdgeTopology processes the shared boundary one ORIGINAL fine
//       segment (one ON_BrepEdge) at a time and can only RELOCATE an
//       EXISTING boundary vertex on each side to a shared chord fraction
//       - it cannot manufacture a wall-side vertex that TessellateGrid-
//       ClippedExact's own coarser grid never produced in the first
//       place, so a fine segment whose endpoints fall between two of the
//       wall's (far sparser) grid corners has no matching run on that
//       side and is left for StitchTJunctionsOnce()'s coarser fallback,
//       which the wall's curvature-bowed-off-chord geometry (this file's
//       own earlier session) already defeats. This is a SEPARATE,
//       deeper gap than (1) - a resolution mismatch between two
//       independently-chosen sampling densities, not a vertex being
//       dropped - one level up from TessellateGridClippedExact, in
//       either ReconcileEdgeTopology's own per-edge walk (boolean_
//       general.cpp) or in giving TessellateGridClippedExact a way to
//       honor a denser trim boundary's own intermediate points along a
//       straight cut, not just its two endpoints, when the grid is
//       coarser than the trim. A concrete next-increment target, not
//       explored further this session.
//   (2) A SEPARATE, larger structural gap, found while isolating (1)'s
//       own residual: an "untouched" operand face BooleanCombineGeneral
//       keeps wholesale (no intersection curve touches it at all, e.g. a
//       cylinder's own end cap once the cut only touches its wall) did
//       NOT get welded through the same VertexWelder/BuildLoop() identity
//       mechanism a freshly-cut NEIGHBORING fragment does - so the two
//       shared no real ON_BrepEdge at all, even though they meet at
//       identical 3D points (this file's own "friendless cylindrical
//       band" notch precedent, brep.cpp/brep.h, is the same shape of gap
//       one level up).
//
//       CLOSED (a later session): root-caused to FaceBoundaryLoop()
//       (boolean_general.cpp) resampling each face's own trim/edge curve
//       at a fixed `samples_per_edge` FRACTION of ITS OWN parameter
//       domain, independently per face - for a boundary two faces
//       genuinely share (e.g. a solid cylinder's disk cap and its own
//       wall, built by Brep::FromMixedFaces() from the SAME dense point
//       ring, confirmed directly), each side's own trim has a DIFFERENT
//       parameterization (the cap's a dense polyline indexed by vertex
//       count, the wall's a plain 2D line whose 3D image is the true
//       isocurve circle indexed by angle), so sampling both at the same
//       `i / samples_per_edge` fraction lands at a different physical
//       angle on each side past the shared endpoint. Added
//       ReconcileFragmentBoundaries(), a NEW pass at fragment-assembly
//       time (BEFORE the real VertexWelder/BuildLoop() pass, not
//       ReconcileEdgeTopology's own tessellation-time one): it welds a
//       throwaway detector over every kept fragment's own loop to find
//       "anchor" positions already coincident with some OTHER fragment,
//       splits each loop into anchor-to-anchor runs (including the
//       degenerate but real "one single anchor, the whole loop is one
//       run back to itself" and "two DIFFERENT positions on one loop that
//       share one anchor vertex, because that loop continues past it
//       toward a completely different neighbor" cases - both measured
//       directly on box+cylinder's own cap/wall boundary), and whenever
//       exactly two runs from two DIFFERENT fragments share an anchor
//       pair AND a multi-probe geometric vote confirms they trace the
//       SAME physical curve (rejecting a same-corner-only false match,
//       also measured directly to occur elsewhere in the sweep), reuses
//       the denser run's own points VERBATIM on the sparser side - so the
//       real welder below is guaranteed to merge them into one shared
//       vertex per point, giving BuildLoop() a genuine, TrimCount()==2
//       ON_BrepEdge to hand ReconcileEdgeTopology afterward, exactly as a
//       chain-cut edge already gets.
//
//       Measured directly on box+cylinder Union (the disclosed fixture
//       above): the result's own naked (TrimCount()==1) ON_BrepEdge count
//       dropped from 120 to 20 - the cap/wall boundary itself now fully
//       shared (0 naked, down from 52 combined) - ON_Brep::IsValid() and
//       the tessellated volume unaffected (both already correct before
//       and after, confirmed by direct measurement, not merely inferred
//       from the edge count). The sweep's own aggregate closedmesh count
//       did NOT move (still 15/76, identical case set, no reshuffling):
//       for box+cylinder specifically, ReconcileEdgeTopology's own
//       DINO8_RECONCILE_DEBUG trace shows it never even walks these now-
//       real cap/wall edges (their own tessellated boundaries already
//       agree with no T-junction to insert - the topology fix genuinely
//       worked), yet DiagnoseManifold's own mesh-boundary count still
//       shows most of its residual sitting exactly at the rim (z = the
//       wall's own v=0/v=length grid lines) - consistent with, not a new
//       instance of, gap (1) above (TessellateGridClippedExact's own
//       grid-alignment vertex-dropping bug, EXPLICITLY out of scope for
//       this session, is worst exactly on a v=const grid line, which a
//       rim by construction always is). This fix stands on its own
//       (confirmed correct in isolation) but needs (1) fixed too, on some
//       later session, before its effect can show up in the aggregate
//       closedmesh count for a curved-vs-curved case like this one.
//
//   (1), continued (a LATER session): the resolution-mismatch gap above -
//       ReconcileEdgeTopology's own per-edge walk cannot manufacture a
//       wall-side vertex that TessellateGridClippedExact's own coarser
//       grid never produced - is closed at the SOURCE instead of at
//       ReconcileEdgeTopology's own layer (the doc comment above laid out
//       both directions; this session investigated both before choosing).
//       Approach (1) - extending ReconcileEdgeTopology's own per-edge walk
//       to INSERT a missing point on the coarser side - turns out not to
//       be viable AS WRITTEN: it processes ONE original fine ON_BrepEdge
//       at a time (a short span between two consecutive BuildLoop()
//       points), and its own walk requires an EXISTING vertex near BOTH
//       endpoints on BOTH sides before it can even start (NearestBoundary-
//       Start). For box+cylinder Union's own 224 fine edges along the cut,
//       the wall's own u_divisions-resolution grid corners are far sparser
//       than the box's fine edges, so MOST fine edges have NEITHER
//       endpoint anywhere near an existing wall vertex - the walk fails
//       to even START, not merely fails to find a clean run. Grouping
//       consecutive fine edges into one coarser reconciliation instead
//       would mean approximating a REAL multi-edge span of the true curve
//       with one much longer chord (a materially worse approximation than
//       the existing per-fine-edge chord), and - more fundamentally -
//       would mean this pass genuinely rewriting real ON_BrepEdge
//       topology mid-tessellation, not just patching a mesh in place.
//       Approach (2) - TessellateGridClippedExact learning about a denser
//       neighbor's own intermediate points along a shared straight cut -
//       turned out to need NO actual cross-face communication at all:
//       Brep::Tessellate()'s own ResolveFace()/SampleLoop() already builds
//       `trim_polygon` by walking EVERY real ON_BrepTrim of a face's own
//       loop and taking one sample per trim (BuildLoop() gives each
//       original fine polyline segment its own trim, and SampleLoop()
//       takes `samples=1` for a linear trim) - so `trim_polygon`, BEFORE
//       SimplifyCollinearRuns() ever runs, ALREADY carries the wall's own
//       full BuildLoop-fine resolution, matching the box side's exactly
//       (confirmed directly: box+cylinder Union's wall trim carries 104
//       points for a 4-corner rectangle, same as 413c0ae's own earlier
//       measurement). SimplifyCollinearRuns() is precisely what erases
//       them again, to protect ClipConvex's own inside test (see its own
//       doc comment above) - the fix is to give TessellateGridClippedExact
//       a way to remember what it erased and put it back afterward,
//       without ever handing ClipConvex the redundant, noise-prone
//       version.
//
//       Shipped: SimplifyCollinearRuns() now also returns every point it
//       dropped, each tagged with the two SURVIVING simplified-trim
//       vertices its own collapsed run sat between (RemovedTrimPoint,
//       surface.cpp) - not just its bare (u, v) position. TessellateGrid-
//       ClippedExact buckets these by which grid cell's own (u, v)
//       rectangle contains each one (O(1) lookup per cell, not O(cells x
//       removed points) - the same performance discipline the earlier-
//       rejected EnsureBoundaryVertex repair was rejected for missing),
//       then a new InsertForcedPointsIntoTriangulation() fans each
//       relevant cell's own forced points into whichever triangle
//       EarClipTriangulate() already gave that boundary span - the SAME
//       "replace one owning triangle with a fan through its apex"
//       technique ReconcileChainToChord (boolean_general.cpp) already
//       uses, just one layer earlier (on a single grid cell's own small
//       polygon, before mesh assembly, not on the whole assembled mesh
//       after it). EarClipTriangulate() itself is deliberately never
//       handed the grown point set - only the cell's own bare
//       grid-clip boundary, however many forced points get fanned in
//       afterward - see that function's own doc comment for why (a real,
//       measured performance regression: feeding EarClipTriangulate() the
//       augmented polygon directly roughly DOUBLED the 76-case sweep's
//       own wall-clock time, 63s -> 125s, before this fix; restored to
//       ~79s with the fan-insertion approach instead - still a real,
//       bounded ~25% increase over the pre-fix baseline, from genuinely
//       reinserting ~15,600 real boundary points across the sweep's own
//       76 cases, not from any remaining algorithmic blowup).
//
//       ONE REAL BUG found and fixed before this was safe to ship: a
//       forced point's bare (u, v) position alone cannot tell "this is
//       the trim's own cut boundary" apart from "an ordinary interior
//       grid-line edge that merely happens to run the SAME direction" -
//       for an AXIS-ALIGNED cut (the common, previously-rock-solid
//       box+box case), the cut's own direction routinely coincides
//       exactly with a plain u=const or v=const cell edge direction. An
//       early version tested only "is this forced point collinear with
//       the candidate cell edge", which happily matched an ordinary
//       shared grid-line edge between two cells - fanning a point into
//       ONE cell's copy of that edge while its untouched neighbor cell
//       kept the un-subdivided original, opening a small crack. Caught by
//       this repo's own ctest suite (dino8_kernel_smoke), NOT the sweep:
//       box+box Union and Difference's own previously-Mesh::IsClosedManifold()
//       TessellateGeneralBooleanClosedMesh() results broke. Fixed by
//       requiring BOTH of the candidate cell edge's own endpoints - not
//       just the forced point itself - to sit on the SAME originating
//       trim edge's own line (RemovedTrimPoint's own `edge_t0`/`edge_t1`,
//       carried from SimplifyCollinearRuns() through bucketing to the
//       fan-insertion check itself): a genuine trim-cut edge segment lies
//       ON that exact line by construction; an ordinary cell edge that
//       merely runs parallel to it does not (unless the cut happens to
//       sit exactly on a grid line, the pre-existing, separately-disclosed
//       degeneracy above - a narrower, real edge case, not this bug's
//       broad failure mode). A second, unrelated attempt at raising the
//       box+cylinder Union nonmanifold-edge count back down (a FOURTH
//       degenerate-triangle drop pass after the final cross-face weld,
//       targeting a handful of weld-time near-duplicate-vertex artifacts)
//       was tried and REVERTED for the same reason as the box+box
//       regression above: it is the LAST pass with nothing after it to
//       re-stitch whatever it strands, so it reopened box+box Union/
//       Difference again (caught the same way, by ctest, not the sweep).
//       Not shipped; see this file's own next-increment note below.
//
//       MEASURED, not assumed: tests/general_boolean_sweep.cpp's own
//       76-case sweep: 15/76 -> 16/76 (box+box, second box rotated
//       30deg, Union newly closes - the sweep's OWN case set, no
//       reshuffling of any previously-closing case). box+cylinder Union's
//       own naked-boundary-edge count (DiagnoseManifold, scratch_test.cpp,
//       this file's own disclosed fixture, u_divisions=8/v_divisions=32):
//       378 -> 334, an honest ~12% reduction, not the full close this
//       gap's own root cause would suggest - see the next-increment note
//       below for exactly what's left. Its own non-manifold-edge count
//       moved 4 -> 10, a real, disclosed, NOT-fixed-this-session side
//       effect - all 6 new ones sit at an UNRELATED location (near the
//       cylinder's own z rim/cap seam, not this fix's own target cut
//       boundary), same general shape (a near-duplicate vertex pair that
//       StitchTJunctionsOnce's own chain insertion leaves a hair's width
//       apart) as the 4 that were ALREADY there before this session,
//       just reshuffled by this fix's own upstream effect on which edges
//       ReconcileEdgeTopology reconciles first. Full dino8-kernel ctest
//       suite (dino8_kernel_smoke, 1663 checks): 100% pass, 147.35s wall
//       clock - matches this suite's normal ~150s+ runtime, no
//       regression (the sweep's own ~25% slowdown above is confined to
//       tests/general_boolean_sweep.cpp, which is deliberately NOT
//       registered with ctest - see its own top comment).
//
//       NEXT INCREMENT (CLOSED, a later session): the weld-time near-
//       duplicate-vertex coincidence above got its real fix at its own
//       source, in StitchTJunctionsOnce's own chain-insertion (boolean_
//       general.cpp), not a triangle drop after the fact - see that
//       function's own doc comment for the full mechanism. DIAGNOSIS
//       (DINO8_RECONCILE_DEBUG plus direct instrumentation of Stitch-
//       TJunctionsOnce itself): box+cylinder Union's z rim (where the
//       cylinder's own wall meets its own cap - a fully PERIODIC boundary)
//       falls entirely to StitchTJunctionsOnce's own fallback, never
//       ReconcileEdgeTopology (confirmed: NearestBoundaryStart fails on
//       the cap side for every one of that rim's real edges - the cap's
//       own genuinely-curved-in-(u,v) grid-clip boundary lands 0.0003-
//       0.001 away from the wall's real edge vertices there, a SEPARATE,
//       not-closed-this-session instance of the resolution-mismatch gap
//       above, this time on a curved rather than straight trim). Because
//       StitchTJunctionsOnce is fed EVERY other face's own vertex as a
//       hit candidate for a boundary edge (not just the true topological
//       neighbor), the cap's denser sampling there routinely contributes
//       several genuinely-distinct-but-mutually-adjacent hits (confirmed:
//       up to 3 within ~0.0003 of each other) that each individually pass
//       PointStrictlyOnSegment (which is blind to the other hits found
//       for the same segment) - fanning them all in as separate vertices
//       produces slivers thin enough that the two faces' boundaries no
//       longer agree which vertex is "the" corner there: a nonmanifold
//       edge, not a mere T-junction.
//
//       FIX: widen StitchTJunctionsOnce's own existing hit-vs-hit de-dup
//       (previously a tiny, fixed `tol`) to the SAME scale-aware distance
//       PointStrictlyOnSegment already uses for its own perpendicular-
//       distance acceptance (floored at `tol`, else a fraction of the
//       segment's own length), at 2x that formula's own coefficient -
//       scoped DELIBERATELY to hit-vs-PRIOR-HIT only, never hit-vs-the-
//       segment's-own-endpoint: an equivalent endpoint-relative version
//       was tried FIRST and REJECTED - even a hit genuinely close to a
//       real endpoint is a normal, often NECESSARY case elsewhere in this
//       engine (this file's own resolution-mismatch forced-point
//       mechanism routinely places one there), and rejecting it broke
//       box+box (second box rotated 30deg about z)'s own previously-
//       closing B-A case in the 76-case sweep at every coefficient tried,
//       including ones far too small to help box+cylinder at all. An
//       equivalent triangle-area-ratio formulation of the same
//       endpoint-relative idea was also tried and also rejected the same
//       way - confirmed directly, on this same fixture, that no single
//       distance or area-ratio threshold cleanly separates "duplicate"
//       from "legitimate" once endpoints are included, since their own
//       scales genuinely overlap. Hit-vs-prior-hit alone has no such
//       conflict and was measured clean up to 20x its own coefficient
//       with no further benefit and no new regression either.
//
//       MEASURED: box+cylinder Union's own nonmanifold-edge count
//       (DiagnoseManifold, scratch_test.cpp, u_divisions=8/v_divisions=
//       32): 10 -> 6 (the 965ee6b session's own 4-edge PRE-regression
//       baseline is not quite reached - of the remaining 6, 4 (at z=-1 and
//       z=1, the wall/box CUT boundary itself) are the SAME 4 edges
//       965ee6b's own disclosure already named as pre-existing there -
//       gap (1)'s own resolution-mismatch residual, not this rim's
//       periodic-seam defect, and UNCHANGED by this session's fix, as
//       expected; the other 2 (still at the z rim) are one more
//       occurrence of this SAME rim defect that hit-vs-prior-hit
//       clustering alone cannot reach, since it is a single isolated hit
//       near a segment's own endpoint, not a mutually-close cluster - the
//       curved-trim resolution-mismatch gap above is this residual's own
//       real next increment, not a further StitchTJunctionsOnce tweak).
//       tests/general_boolean_sweep.cpp's own 76-case sweep: unchanged at
//       16/76, byte-for-byte the same case set (diffed directly) - zero
//       reshuffling. Full dino8-kernel ctest suite (dino8_kernel_smoke,
//       1663 checks): 100% pass, 155.26s wall clock, matching this
//       suite's normal runtime - no regression.
//
//   NEW LEAD (a later session, after the 76-case sweep's own boolean-
//       CORRECTNESS gap - volume/ON_Brep::IsValid()/nonsimple-trim - was
//       separately closed to 76/76): re-measured the closedmesh gap
//       itself (now 17/76) and found every prior fix in this file's own
//       history above targets exactly one failure signature -
//       Mesh::IsClosedManifold()'s own undirected-edge-count check
//       (count != 2: a naked or nonmanifold edge) - never its OTHER,
//       independent check: `orientation_consistent` (a directed edge
//       walked twice - two triangles both claiming the same edge in the
//       SAME winding direction). Added a DINO8_MESH_DEBUG diagnostic to
//       IsClosedManifold() itself (mesh.cpp) and confirmed directly: box+
//       cylinder Union/Intersection/A-B/B-A ALL report
//       orientation_consistent=0, while every currently-CLOSED case
//       (box+box) reports orientation_consistent=1 - this check is
//       genuinely meaningful here, not a chronic false positive.
//
//       Tried the same "insert a bracketing vertex" idea (2) above landed
//       on conceptually, generalized to ReconcileEdgeTopology's own
//       vertex-proximity walk (a new FindBracketingBoundaryEdge/
//       InsertBracketedSpan pair, splitting a coarse boundary edge's own
//       owning triangle when NEITHER of a finer neighbor's shared edge
//       endpoints sits near any vertex on the coarse side at all - the
//       genuinely different case (2)'s own fix above didn't reach, per
//       (1)'s "next-increment" note earlier in this comment). Measured,
//       not shipped: it triggered 16 times on box+cylinder Union but the
//       full 76-case sweep's own output was byte-for-byte UNCHANGED, and
//       box+cylinder Union's own naked-edge count went UP slightly (1177
//       -> 1183) rather than down - reverted rather than ship a change
//       with no verified benefit.
//
//       ISOLATED FURTHER: instrumented TessellateGeneralBooleanClosedMesh
//       itself to merge-and-check `result.Tessellate()`'s own RAW per-face
//       output BEFORE any of this file's own StitchTJunctionsOnce/
//       ReconcileEdgeTopology/ReconcileChainToChord passes run at all.
//       That RAW merge is ALREADY orientation_consistent=0 for box+
//       cylinder Union (bad-edge-count=2696), and stays orientation_
//       consistent=0 after every reconciliation pass runs (bad-edge-count
//       drops to 1177 - real, substantial progress on the naked-edge
//       axis, exactly matching this file's own long history above - but
//       the orientation flag itself never moves). This rules out
//       StitchTJunctionsOnce/ReconcileChainToChord/ReconcileEdgeTopology
//       as the SOURCE of the orientation conflict (their own fan-
//       insertion was independently re-checked by hand for winding
//       preservation and found consistent: every fan triangle is built
//       as {apex, chain[k], chain[k+1]} in the same cyclic order the
//       original triangle's own directed edge already carried) - the
//       true source is upstream, in Brep::Tessellate()'s own per-face
//       generation (a face-level m_bRev/FlipNormals defect on some
//       specific face of a BooleanCombineGeneral result?) or in the raw,
//       topology-blind Mesh::MergeAndWeld() itself. NOT diagnosed further
//       this session - a genuinely fresh, precisely-scoped next-increment
//       target, orthogonal to every naked-edge-count fix documented
//       above.
//
//       FURTHER ISOLATED (a still later session): added a DINO8_FACE_
//       ORIGIN_DEBUG diagnostic directly in TessellateGeneralBooleanClosed
//       Mesh (before Mesh::MergeAndWeld() discards per-face provenance) -
//       an independent, weld-by-rounding pass over `faces` (the per-ON_
//       Brep-face MutFace list) that re-derives the same duplicate-
//       directed-edge conflict but tags each triangle with its origin
//       ON_Brep face index, so the two conflicting triangles can be traced
//       back to which face(s) produced them. On box+cylinder Union, the
//       FIRST conflict is between ON_Brep face 0 (m_bRev=0 - the box's
//       bottom cap, which after the boolean carries a hole loop where the
//       cylinder passes through it) and, at the SAME directed edge
//       (v1110->v1249, on the box's bottom-cap hole boundary), THREE
//       different candidate cylinder-wall fragment faces show up across
//       repeated runs of the same detector (faces 8 and 2, both m_bRev=0,
//       and face 6, m_bRev=1) - i.e. more than one fragment face's own
//       triangulation is claiming a triangle incident to this exact
//       boundary edge. This is NOT simply "one face's m_bRev is flipped
//       relative to its neighbor" (that would show a single consistent
//       origin-face pair every time) - it looks instead like the box's
//       hole-boundary loop and the cylinder-wall fragment(s) that should
//       meet it are not cleanly 1:1: either the hole loop itself is
//       duplicated/overlapping in the notch-composition that built face
//       0's trim, or more than one cylinder-wall KeptFace fragment
//       independently believes it owns this same seam segment. Confirmed
//       behavior-preserving: the diagnostic added in mesh.cpp's own
//       IsClosedManifold() (captures the first duplicate-directed-edge's
//       two vertex ids/coords and the two ON_MeshFace records involved)
//       and this file's own DINO8_FACE_ORIGIN_DEBUG block are both
//       print-only - the 76-case sweep's output is byte-for-byte
//       unchanged with them compiled in, and the full ctest suite (dino8_
//       kernel_smoke) is 100% green.
//
//       ROOT CAUSE, FIRST PASS (same later session, continued - SEE
//       CORRECTION FURTHER BELOW, this pass's own "3-claimant" mechanism
//       was disproven by a closer read of its own debug output): found
//       that ReconcileFragmentBoundaries() (this file) explicitly skips
//       reconciling any anchor-vertex-pair whose key has `rs.size() != 2`
//       ("ambiguous (0, 1, or 3+ claimants) - leave alone", see the
//       `continue` right after this function's own `total_pairs`/
//       `two_run_pairs` debug counters), and added a per-pair DINO8_
//       BOOL_DEBUG dump of every such skipped pair's owning loops/
//       positions to see which. Originally guessed (WRONG, see below)
//       that BridgeHolesIntoOuter()'s keyhole notch pushes these pairs to
//       3 claimants via a same-face double-touch.
//
//       CORRECTION (reading the actual dump output, not just the
//       mechanism it plausibly suggested): of the 444 anchor-pair keys on
//       box+cylinder Union, only 18 are not cleanly 2-run, and of THOSE,
//       17 have exactly ONE run total (not 3) - meaning no matching
//       cross-face partner was found for them at all, a different failure
//       shape than "ambiguous 3+ claimants". The apparent "three
//       different candidate faces" in the FACE_ORIGIN_DEBUG finding
//       above was three separate op EXECUTIONS (Union/A-B/B-A, each its
//       own independent BooleanCombineGeneral call hitting its own first
//       conflict) reported together, not three simultaneous claimants of
//       one physical edge within a single mesh - conflating those was
//       this pass's own mistake, corrected here rather than left
//       standing. (The one truly-2-run-but-skipped pair found is a
//       legitimate same-face self-seam - both runs on the same kf - and
//       is correctly left alone by the existing same-kf `continue`.)
//
//       SECOND CORRECTION (this investigation's own second wrong guess,
//       also caught before shipping any code - the pattern of "plausible
//       mechanism, verified wrong on closer inspection" recurred and is
//       recorded honestly rather than smoothed over): a follow-up attempt
//       assumed kf0's 73-point span was ENTIRELY foreign to kf8 (a coarse-
//       vs-fine resolution mismatch across the whole arc) and prototyped a
//       ReconcileFragmentBoundaries generalization on that basis. Directly
//       dumping BOTH loops' own anchor lists point-by-point (not just
//       counts) disproved this immediately: kf8 actually DOES carry 72 of
//       kf0's 73 intermediate vids as its own individual anchors (walking
//       the same arc in the opposite direction, exactly as this file's own
//       "adjacent faces trace their shared boundary in opposite senses"
//       convention expects) - so essentially all of that span already
//       reconciles fine-for-fine via the ordinary per-step path, with NO
//       edit needed (a already-matching single-segment pair is silently
//       skipped, which is why no "reconciled key=" log line was ever
//       printed for it - not evidence that reconciliation wasn't
//       happening). The prototype fix never fired for exactly this
//       reason, and was reverted rather than left in the tree unused.
//
//       CONFIRMED, PRECISELY AND FINALLY (this time by adding a targeted
//       print directly at BridgeHolesIntoOuter()'s own accepted-candidate
//       site and matching its EXACT numbers against the (38,111) gap's own
//       coordinates - no more inference from counts or nearby-but-not-
//       identical evidence): the (38,111) gap IS BridgeHolesIntoOuter()'s
//       own kEdgeFraction=1e-3 splice, exactly as this file's own earlier,
//       already-written "ONE MORE WRINKLE" note below described - an
//       EARLIER pass through this same investigation (immediately above,
//       in an intervening commit) incorrectly concluded the two were
//       "unrelated" from position/count reasoning alone; that conclusion
//       is retracted here now that the actual numbers are in hand. On box+
//       cylinder Union's accepted hole attachment: h_j.p = (0, 1, -1)
//       (the chosen hole-boundary pinch vertex, at the circle's true north
//       pole); the notch's own c_in = LerpOnSurface(h_j, h_next, 1e-3) =
//       (-1.20496156e-4, 0.999992714, -1); c_out = LerpOnSurface(h_prev,
//       h_j, 1-1e-3) = (+1.20496156e-4, 0.999992714, -1). These are BIT-
//       FOR-BIT the same two points found straddling the north pole in
//       kf0's own assembled loop (positions 37 and 112) - not merely
//       similar in magnitude. h_j itself is never copied into kf0's own
//       loop at all (the hole-walk loop deliberately starts at h_{j+1} and
//       ends at h_{j-1}, skipping h_j - see the `for (size_t k = (j + 1) %
//       m; k != j; ...)` loop), while kf8's own boundary DOES carry h_j
//       verbatim (unaffected by this file's own notch splicing, since kf8
//       is a plain wall fragment, not a bridged-hole face) - so kf8's own
//       exact h_j anchor has no counterpart on kf0's side within weld
//       tolerance of either c_in or c_out, and the pair goes unreconciled.
//       This is NOT the SplitPeriodicWrapChain/seam-vertex-p family this
//       session fixed once already (f83d6ea) - that fix and this gap are
//       unrelated; the actual mechanism is exactly the deliberate,
//       documented kEdgeFraction perturbation already described below.
//
//       NEXT STEP (not yet implemented - the mechanism is now fully
//       confirmed with matching numbers, but the fix itself still needs
//       real design + testing before landing, given this investigation's
//       own history of wrong first guesses in this exact file): the
//       cleanest fix is likely to preserve h_j's own identity somewhere
//       recoverable rather than only ever emitting the two offset points
//       around it - e.g. have BridgeHolesIntoOuter() record, per
//       attachment, which two ASSEMBLED-array positions (c_in and c_out)
//       morally correspond to which ORIGINAL hole-loop vertex (h_j), and
//       teach ReconcileFragmentBoundaries() to treat a cross-face anchor
//       matching h_j as reconcilable against EITHER of kf0's two straddling
//       points (snapping both, or the nearer one, to h_j exactly) instead
//       of requiring an exact weld. Simply splicing h_j itself into kf0's
//       own array adjacent to c_in or c_out was considered and rejected
//       without writing code: c_in/c_out are deliberately offset TOWARD
//       h_j from the h_next/h_prev side respectively, so placing h_j
//       immediately next to either one would create a short backtracking
//       zigzag (walk toward h_j, then away again) rather than a clean
//       insertion - a real risk of a new self-overlap defect, not a free
//       lunch. Any fix here must be verified against the full 76-case
//       sweep (watch for the closedmesh count moving, and no volume/
//       ON_Brep::IsValid()/nonsimple-trim regression) and the full ctest
//       suite before being considered done - and, given this investigation's
//       track record, re-verified with fresh point-level data (not just
//       counts) before being trusted.
//
//       TRIED AND REJECTED (measured, not guessed): simply shrinking
//       kEdgeFraction (so c_in/c_out's offset from h_j falls below
//       kWeldTol=1e-6, letting them weld to h_j and to the neighbor's own
//       exact point for free, no data-flow changes needed) was tried at
//       1e-7 and at 1e-5. BOTH regressed the 76-case sweep - several
//       previously-OK cases (box+cyl case 01/02/03, cyl+cyl case 07/08,
//       sphere+box case 09, sphere+sphere case 12, box+cone case 15,
//       torus+box case 16) turned into TessellateGridClippedExact
//       exceptions ("trim_polygon must have at least 3 points"), i.e. the
//       notch corner became numerically collinear/degenerate at that
//       fraction for at least one geometry scale in the corpus - exactly
//       the failure mode this constant's own doc comment already warned
//       about. A single global fraction cannot be shrunk safely without
//       either a per-attachment absolute-distance floor (scaled to the
//       LOCAL edge lengths at that specific notch, not the whole model's
//       diagonal) or reworking the collinearity check itself - out of
//       scope for a quick constant tweak. Both values were reverted via
//       `git checkout HEAD` immediately after being measured; neither is
//       in the tree.
//
//       ALSO TRIED AND REJECTED (measured): using h_j directly (no offset
//       at all) for c_in/c_out, on the hypothesis that the collinearity
//       risk kEdgeFraction guards against is specific to the OUTER side
//       (box edges are straight, so o_prev/o_i/o_next are often exactly
//       collinear) and not the hole side (a circle's consecutive samples
//       are essentially never exactly collinear, so the hole-side offset
//       seemed unnecessary for that specific reason) - leaving a_out/a_in
//       untouched and only replacing c_in/c_out with h_j itself. This did
//       NOT reproduce the earlier TessellateGridClippedExact regressions
//       (no collinearity exceptions), but introduced a DIFFERENT, equally
//       real regression: several cases that previously reported OK now
//       report WRONG-VOLUME (box+cyl 01/02/03, cyl+cyl 07/08, box+cone 15)
//       or INVALID (sphere+sphere 12), with volumes off by ~5-10%. Passing
//       BridgeHolesIntoOuter's own IsSimplePolygon()/area-match acceptance
//       checks is evidently not sufficient for a zero-width slit (both
//       ends of the notch at the exact same point) to tessellate/clip
//       correctly downstream - something in the exact-clip tessellation
//       or area accounting treats a literal zero-width pinch differently
//       from a genuinely narrow one, in a way that silently produces the
//       wrong result rather than failing loudly. This confirms BOTH
//       kEdgeFraction offsets (outer AND hole side) are load-bearing for
//       reasons beyond simple collinearity avoidance, not just one of
//       them - a real fix cannot touch the notch's own geometry at all
//       and must instead work at the metadata level (the identity-mapping
//       approach described above). Reverted via `git checkout HEAD`
//       immediately after being measured; not in the tree.
//
//       METADATA-LEVEL FIX IMPLEMENTED AND MEASURED (also not in the
//       tree - kept out because it doesn't close the gap, see below, not
//       because it broke anything): built the identity-mapping approach
//       in full - HoleAttachment gained `skipped_vertex`/`skipped_outer_
//       vertex` (h_j and o_i, recorded at candidate-acceptance time),
//       AssembleWithAttachments gained an optional `insert_starts` output
//       so each attachment's final position in the merged array is known,
//       BridgeHolesIntoOuter threaded a `pinches_out` vector of (position,
//       true-vertex) pairs for all four notch corners (c_in, c_out, a_out,
//       a_in) out to a new `KeptFace::hole_pinches` field, and
//       ReconcileFragmentBoundaries relabeled each pinch position's own
//       weld id (via the SAME `detect` welder already in scope) to its
//       true target's id right after the normal per-point weld pass,
//       before anchors are computed - touching NO geometry at all, only
//       the internal id bookkeeping. Measured: the 76-case sweep stayed
//       byte-for-byte unchanged (no regression, confirmed), and on box+
//       cylinder Union the anchor-pair bookkeeping genuinely improved (14
//       unresolved pairs down to 6, cross-checked point-by-point - the 2
//       new "2-run" pairs that appeared are legitimate same-face self-
//       seams, correctly left alone by the pre-existing same-kf check).
//       BUT Mesh::IsClosedManifold()'s own bad-edge-count on that exact
//       fixture was completely UNCHANGED (1177, identical to before) -
//       the fix has zero effect on the actual output mesh. Root cause of
//       THAT: every pair this fix resolves is already a trivial single-
//       segment span on both sides (`ga.pts.size() <= 2 && gb.pts.size()
//       <= 2` in the existing reconciliation code), so the fix changes
//       nothing about which points get inserted where - it only avoids a
//       wasted/ambiguous-looking match attempt. The two ACTUAL physical
//       points (c_in/c_out's own real 3D coordinates, still offset from
//       h_j by kEdgeFraction as always) are never moved or merged by this
//       fix, and the ACTUAL weld that matters happens much later, in
//       Mesh::MergeAndWeld() (TessellateGeneralBooleanClosedMesh, this
//       file), which only ever sees raw 3D coordinates post-tessellation
//       - it has no visibility into ReconcileFragmentBoundaries's own
//       (pre-tessellation, 2D-trim-loop-level) weld-id bookkeeping at
//       all. A fix that actually closes this gap must therefore act at
//       the MESH level (inside TessellateGeneralBooleanClosedMesh, after
//       `result.Tessellate()` produces `raw_faces`/`faces`, before the
//       final `Mesh::MergeAndWeld(patched, tol)` call) rather than at the
//       2D boundary-reconciliation level this section has been probing -
//       genuinely new architectural territory (the pinch identity would
//       need to survive from BridgeHolesIntoOuter's own 2D loop all the
//       way through BuildLoop -> ON_BrepLoop/ON_BrepTrim -> brep.cpp's
//       own per-face tessellation, which does not currently preserve any
//       such provenance) - out of scope for this pass. Reverted via `git
//       checkout HEAD` after measuring; not in the tree.
//
//       ONE MORE WRINKLE (found while scoping the fix above): BridgeHoles
//       IntoOuter()'s own notch (this file, above) does not even splice in
//       the hole loop's own EXACT pinch vertex - it inserts the two fresh
//       LerpOnSurface(..., kEdgeFraction=1e-3) points on both the outer
//       side (o_i/o_next) and the hole side (h_j/h_prev) described above,
//       a deliberate near-miss so the notch's own corners are never
//       exactly collinear/zero-area (this function's own top comment
//       explains why - IsConvexPolygon()'s 1e-12 collinearity threshold).
//       This IS the (38,111) gap's own root cause, confirmed above, not a
//       separate concern layered on top of it.
//
//   MESH-LEVEL SEAM REPAIR (a later session - SHIPPED, measured): the
//       "act at the MESH level" next step above is implemented as
//       RepairMergedSeams() (boolean_general.cpp, see its own doc comment
//       for each operation's exact rule), run by TessellateGeneralBoolean-
//       ClosedMesh on the ONE mesh Mesh::MergeAndWeld() returns, after
//       every per-face pass above. It touches only edges whose undirected
//       count is not 2 (naked / nonmanifold) and the vertices on them, so
//       a mesh that is already IsClosedManifold() passes through untouched
//       - a no-op on every previously-closing case by construction.
//       DINO8_NO_SEAM_REPAIR=1 disables it; DINO8_SEAM_REPAIR_DEBUG=1
//       prints per-iteration counts, =2 also dumps every residual edge.
//
//       MEASURED (tests/general_boolean_sweep.cpp, 76 cases, diffed case-
//       by-case against the pre-change baseline): non-EMPTY closedmesh=0
//       count 55 -> 19 (36 cases newly closed, verified key-by-key against
//       the baseline output, not just the aggregate count). ZERO
//       regressions of any kind (no closedmesh 1->0, no valid 1->0, no
//       OK -> other verdict, no exception - all 76 cases stay "| OK").
//       box+cylinder Union (tests/general_boolean_sweep.cpp's own case 01,
//       built via that file's FrameFromAxis cylinder frame) goes from
//       IsClosedManifold's `orientation_consistent=0 bad-edge-count=1177`
//       to `orientation_consistent=1 bad-edge-count=0`, and all four of
//       its ops close. Volumes moved on 49 ops, 40 of them TOWARD the
//       closed-form value: every box+cylinder-01 op now sits at err
//       0.040-0.041, which is exactly the tessellation's own inherent
//       inscribed-32-gon deficit (0.64% of pi r^2 h) - the previously-open
//       meshes were off by up to 0.073 - and no op's error grew by more
//       than 0.011 (all far inside the sweep's own 2% tolerance). Full
//       ctest suite (dino8_kernel_smoke, 1663+ checks): 100% pass, 241.37s
//       wall clock. tests/general_boolean_sweep.cpp's own 76-case sweep:
//       150.70s wall clock (`time` on the standalone binary).
//
//       NOT UNIFORM ACROSS SEAM ANGLE (found while wiring up ctest
//       assertions on this): tests/test_basic.cpp's OWN longstanding
//       box+cylinder fixture (TestBooleanCombineGeneralBoxCylinder,
//       MakeCylinderZForBoxCylinderTest - this file's own disclosed
//       fixture throughout its history, box (-2,-2,-1)-(2,2,1), cylinder
//       radius 1 z in [-2,2]) is the SAME shape as the sweep's case 01
//       above, but built with a DIFFERENT cylinder frame convention
//       (xaxis=(1,0,0), i.e. its wall's own u=0 periodic seam sits at
//       physical angle 0, point (1,0,z) - the sweep's own FrameFromAxis
//       picks xaxis=(0,-1,0) for the same +z axis, seam at angle 90deg).
//       Measured directly (tests/scratch_test.cpp's own STANDALONE-REPRO
//       block, deterministic across repeated runs - confirmed NOT a
//       full-ctest-suite-context artifact): at seam angle 0, Union and
//       Difference EACH retain a 4-edge residual (Intersection alone
//       closes); the identical shape with the seam rotated 90deg (i.e.
//       the sweep's own case 01) closes on all four ops, confirming the
//       shape itself is not the variable - only the seam's absolute
//       angle is. Root-caused with DINO8_SEAM_REPAIR_DEBUG=2: the 4
//       residual edges form one small quadrilateral hole (at the wall's
//       u=0 seam, which the seam-angle-0 fixture places exactly on the
//       box-cap cut circle's own x-axis crossing) whose TWO possible
//       triangulating diagonals are BOTH already saturated (undirected
//       count 2) by the box cap's own and the wall's own separately-
//       closed local tessellation - no 2-triangle fan using only the
//       hole's own 4 existing corners can close it without pushing a
//       diagonal to count 3, and this pass never invents a new interior
//       vertex to sidestep that (a genuinely different mechanism from
//       every residual shape (a)-(d) below, none of which involve BOTH
//       diagonals of a hole already being spoken for). Not pursued this
//       session: a targeted "collapse a proper edge anyway when both its
//       endpoints are otherwise-unmatched and small relative to local
//       scale" rule was considered and rejected without writing code -
//       this file's own investigation above already measured twice
//       (kEdgeFraction shrink, then h_j-direct) that loosening exactly
//       this kind of guard on close-but-real edges reliably regresses
//       OTHER cases in the sweep, and the payoff here is one seam-angle-
//       dependent fixture, not a broad case class. tests/test_basic.cpp's
//       ctest assertions were scoped to match this measurement exactly
//       (IsClosedManifold() asserted for Intersection only, volume-match
//       asserted on all three ops' closed-mesh results either way).
//
//       WHY THE SUGGESTED "mutually-nearest broken-vertex merge" ALONE
//       WAS NOT ENOUGH (measured on box+cylinder Union at 32x128 with a
//       standalone diagnostic re-deriving IsClosedManifold's edge counts
//       on the final mesh): the residual is NOT mostly near-duplicate
//       vertex pairs. Of 1122 broken vertices only 359 are mutually-
//       nearest pairs and only 20 are within 1e-4 of their partner; the
//       1171 naked edges form 48 chain components of up to 74 vertices,
//       i.e. long stretches where the two faces sample the SAME seam with
//       different point sets that never coincide at all - e.g. one face
//       carrying a single 0.07-long chord across a span the other face
//       covers with five short edges through four true-circle points ~6e-4
//       off that chord (its sagitta, ~0.9% of its length, past the per-
//       face passes' 5e-3-of-length acceptance). A merge-only pass got
//       1171 -> 1071 naked (53 pairs) and stopped. The kEdgeFraction pinch
//       this file's investigation ends on IS there (c_in / h_j / c_out,
//       pairwise ~1.2e-4 apart) and IS what the merge step closes - but
//       as a 3-WAY tie, which is exactly why "merge if closer than a
//       fraction of the SHORTEST incident edge" (the first thing tried)
//       vetoes every one of its pairs: each pair's own third point is an
//       equally short edge away. Keying the radius to the LONGEST incident
//       edge (the surrounding mesh's own cell size, 0.1x) fixed that.
//
//       The four further operations (all local, all bounded, all only ever
//       on broken edges) that the residual's own measured shapes demanded:
//       (a) naked zip - a naked vertex fanned into the nearest non-incident
//           naked edge that one of ITS OWN naked edges runs antiparallel
//           to (the other face's copy of the seam; a face's own next edge
//           is parallel and never matches), within 0.02 / 0.05 / 0.1 of
//           that edge's length coarse-to-fine, t in [0.02, 0.98]: 1071 ->
//           198 naked;
//       (b) sub-cell hole fill - a remaining naked loop of <= 8 vertices
//           no wider than two local cells, fanned closed wound opposite to
//           its naked edges: the cap/wall rim, where the cap's grid
//           crossings sit up to ~4e-3 radially inside the wall's exact
//           circle points - 25% of the 0.017 local edge length, past any
//           sane relative tolerance: 198 -> 0 on Union;
//       (c) flap flip - a triangle whose apex is in no other triangle, on
//           a base walked in the SAME direction by another face: it and
//           its own-face partner tile a quad with the wrong diagonal;
//           re-diagonalizing leaves a plain naked T-junction (a) closes.
//           Pre-existing (not created here) on box+cyl Intersection/B-A;
//       (d) fold / duplicate removal - two triangles on the same three
//           vertices (opposite winding: a zero-thickness fold whose signed
//           volumes and directed edges cancel exactly; same winding: a
//           double cover). A closed manifold cannot contain either, so
//           dropping them is safe by construction. Found - AFTER (a)-(c)
//           had closed every naked edge - to be the ENTIRE residual on
//           box+cone / cyl+cyl / box+cyl(blind) Intersection: a rim
//           sliver fanned in once by each of the per-face passes upstream
//           with opposite winding, at every count-4 edge those ops had.
//       An admissibility guard on (a) and (b) - a fan triangle may not
//       duplicate an existing one nor push any edge past count 2 -
//       mattered measurably: without it the zip could itself manufacture
//       a count-3 edge or a fold (box+cyl Intersection at 32x128 was left
//       with exactly 4 such edges), and adding it took the sweep from
//       33/76 to 40/76 closed on its own, 7 more cases.
//
//       WHAT IS LEFT (final residual: 19 of the sweep's 76 cases, all
//       still "| OK" on volume - closedmesh=0 on exactly these ops: 05
//       cyl+cyl parallel axes Union/Intersection; 06 cyl+cyl perpendicular
//       equal radii Union/B-A; 07 cyl+cyl perpendicular unequal radii
//       Union/B-A; 08 cyl+cyl SKEW Union/B-A; 09 sphere+box FACE Union/
//       Intersection/A-B; 10 sphere+box EDGE Union/Intersection/A-B; 12
//       sphere+sphere unequal radii Union/Intersection/B-A; 13 sphere+cyl
//       piercing Union/B-A). Four shapes, none of them a near-miss of the
//       rules above:
//       (i)  a ZIGZAG boundary at a cylinder cap rim (cyl+cyl Union, the
//            x=3 cap of A): the cap's naked chain runs BACKWARD along the
//            rim from a wall vertex, then forward past its own start,
//            closed by a chord the wall walks in the same direction as a
//            cap sliver (count 3) - a multi-point version of (c) whose
//            fix would need to know which of two same-direction triangles
//            is "the other face's", provenance MergeAndWeld() has already
//            discarded. Deliberately not guessed at. The same shape is
//            what keeps box+cyl Intersection open at the coarse 8x8 /
//            8x32 (it closes at 16x64 and 32x128).
//       (ii) sphere+sphere Union/Intersection/B-A: the two spheres' own
//            boundary polylines along the intersection circle sit 0.1-
//            0.27 of an edge length apart (0.008-0.013 absolute on a
//            radius-2 sphere) - far beyond a chord's sagitta and beyond
//            any local tolerance this pass could honestly use. An
//            upstream sampling defect, not diagnosed further here.
//       (iii) a handful of near-duplicate vertices the merge step's link
//            condition correctly refuses (merging would create a count-3
//            edge), e.g. sphere+sphere Union's v457/v458 pair 1.9e-4 apart.
//       (iv) a small quadrilateral hole whose TWO possible triangulating
//            diagonals are BOTH already saturated (count 2) by two
//            different faces' own separately-closed local tessellation -
//            not in the sweep's own 76 cases (all of which use this file's
//            FrameFromAxis cylinder frame), but confirmed on tests/
//            test_basic.cpp's own longstanding box+cylinder fixture, whose
//            DIFFERENT cylinder frame convention happens to place the
//            wall's own periodic seam exactly on the box-cap cut circle's
//            axis crossing - see the "NOT UNIFORM ACROSS SEAM ANGLE" entry
//            above for the full mechanism and why it was left open.
//
//       DELIBERATELY NOT DONE: widening any tolerance further. The zip's
//       0.1-of-length ceiling is already 20x the per-face passes' 5e-3 and
//       is safe only because both the point and the edge are ALREADY naked
//       - the same reasoning does not extend to (ii), where the honest
//       statement is that the two boundaries are simply different curves.
Mesh TessellateGeneralBooleanClosedMesh(const Brep& result, int u_divisions = 8, int v_divisions = 8);

}  // namespace dino8::kernel
