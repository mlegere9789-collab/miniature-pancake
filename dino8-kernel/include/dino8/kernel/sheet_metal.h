#pragma once

#include <vector>

#include "dino8/kernel/brep.h"

namespace dino8::kernel {

// Sheet-metal bend allowance (K-factor method, Rhino/SolidWorks/AutoCAD's
// own standard flat-pattern formula; parity-map "Sheet-metal features" -
// previously zero sheet-metal-specific code anywhere in this kernel:
// `UnrollDevelopable()` (surface_edit.cpp) is single-surface unrolling
// only, with no notion of a bend's own material allowance at all): the
// length a FLAT sheet must be cut to before bending, for the curved
// portion of a single bend of `bend_angle_degrees` at a given
// `inside_radius`/`thickness`.
//
// A real bend does not stretch/compress uniformly through its own
// thickness - some internal surface, the "neutral axis", keeps its own
// original length; a factory press bends work-hardened material so that
// axis sits closer to the INSIDE face than the sheet's own geometric
// mid-plane, by `k_factor` (dimensionless, typically 0.3-0.5 for common
// materials/bend ratios; 0.44 is a widely used default) times `thickness`
// out from the inside face. `BendAllowance` is then just that axis's own
// arc length: `bend_angle_radians * (inside_radius + k_factor*thickness)` -
// exactly Bend()'s own arc-length formula below, evaluated at the
// K-factor-adjusted radius instead of the true geometric mid-plane radius
// (`inside_radius + thickness/2`) Bend()'s own construction actually
// sweeps through (see that function's own doc comment for why those are
// deliberately two different, complementary quantities: one is a
// manufacturing approximation for how long to cut the flat blank, the
// other is Bend()'s own real 3D solid's true geometric volume). At
// `k_factor == 0.5` the two coincide exactly - a genuine closed-form
// cross-check, not just a plausible-looking formula.
//
// Throws std::invalid_argument for a non-positive thickness/inside_radius,
// a bend_angle_degrees not in (0, 180), or a k_factor outside [0, 1] (a
// K-factor is a fraction of the thickness measured from the inside face;
// 0 puts the neutral axis AT the inside face, 1 at the outside face -
// anything past either is not a real material behavior this formula
// models).
double BendAllowance(double thickness, double inside_radius, double bend_angle_degrees, double k_factor = 0.44);

// A single-bend sheet-metal part (Rhino/SolidWorks/AutoCAD's own "Bend"
// sheet-metal feature, parity-map "Sheet-metal features" - this kernel's
// first dedicated sheet-metal feature op of any kind): two flat
// rectangular legs of length `leg1_length`/`leg2_length`, width `width`,
// and uniform `thickness`, joined by a genuine curved bend region of
// `inside_radius`/`bend_angle_degrees` - not a sharp crease. Built
// directly (not by deforming an existing flat body): a single closed 2D
// profile - an outer boundary at `inside_radius + thickness` and an inner
// boundary at `inside_radius`, each one leg-arc-leg (two straight
// segments tangent to a shared-center circular arc, the same tangent-
// line-to-arc shape NurbsCurve::FilletCornerArc() (curve.cpp) builds from
// a corner point, here built directly from the known leg lengths and arc
// angle instead), joined end-to-end via NurbsCurve::Join() into one
// closed loop - extruded by `width` via ONE Brep::Extrude() call
// (brep.h).
//
// `leg1_length`/`leg2_length` are measured from each leg's own far (cut)
// end to the NEUTRAL axis's own tangent point (the standard sheet-metal
// "flange length" convention) - not to the inside or outside face, and
// not to the two legs' own hypothetical sharp-corner intersection.
// `bend_angle_degrees` is the angle SWEPT by the bend (0 would be no bend
// at all; the two legs' own directions differ by exactly this angle), not
// the interior angle between them.
//
// Because this sweeps a constant width*thickness rectangular cross-
// section through a genuine circular arc (a real solid of translation +
// rotation, not two rigid flat legs joined by a sharp edge), its own TRUE
// volume follows Pappus's centroid theorem exactly: the two flat legs
// contribute `(leg1_length + leg2_length) * width * thickness`, and the
// curved region contributes `bend_angle_radians * (inside_radius +
// thickness/2) * width * thickness` - the cross-section's own GEOMETRIC
// centroid radius, deliberately NOT the K-factor-adjusted radius
// BendAllowance() above uses for its own, different purpose (see that
// function's own doc comment).
//
// `bend_angle_degrees` is restricted to (0, 180) so the profile stays a
// simple L-bracket-like star-shaped region for typical dimensions
// (Brep::Extrude()'s own capping requirement) - but that requirement can
// still be exceeded well before 180 degrees for some combinations of
// inside_radius/thickness/bend_angle_degrees (confirmed directly,
// dino8_scratch_test: a relatively THICK annular sector's own star-shaped
// kernel shrinks with angle faster than a thin one's does, so a large
// thickness/inside_radius ratio can throw at a moderate angle where a
// thinner one wouldn't), independent of leg length. Picking dimensions
// that stay comfortably inside that boundary is this function's own
// caller's responsibility, the same as any other Extrude()-based feature
// op in this kernel.
//
// Throws std::invalid_argument for a non-positive leg1_length/leg2_length/
// width/thickness/inside_radius, or a bend_angle_degrees not in (0, 180) -
// plus whatever Brep::Extrude() itself throws if the resulting cross-
// section isn't star-shaped after all.
Brep Bend(double leg1_length, double leg2_length, double width, double thickness, double inside_radius,
          double bend_angle_degrees);

// A multi-bend sheet-metal part (parity-map "Sheet-metal features" -
// closes this item's own disclosed "no multi-bend flat pattern" gap):
// generalizes Bend() above from a single bend between exactly two legs to
// a CHAIN of N bends between `leg_lengths.size()` (== N+1) legs - e.g. a
// U-channel, a hat-section, or any other multi-sided convex bent profile a
// real press brake builds as a sequence of sequential bends along one
// flat blank.
//
// `leg_lengths[0..N]` are the N+1 flat segments, in order from one free
// (cut) end to the other, using the exact same "measured from the far end
// to the neutral axis's own tangent point" convention Bend()'s own
// leg1_length/leg2_length already use. `bend_angles_degrees[0..N-1]` and
// `inside_radii[0..N-1]` are bend i's own angle/radius, sitting between
// leg i and leg i+1 - each bend may use a different radius (a real
// multi-radius part), but ALL bends must turn the SAME rotational sense
// (every entry in (0, 180), the identical range/convention Bend()'s own
// single `bend_angle_degrees` already has - there is no second bend there
// to turn the other way against). That restriction is deliberate, not an
// oversight: a chain that reverses direction partway (a Z/S-bend) would
// flip which side of the running path is "inside" partway through, a
// genuinely different construction this function does not attempt - see
// this function's own "Still partial" note in PARITY_MAP.md.
//
// Built the same way as Bend() itself, generalized: walks the chain once,
// placing each bend's own circular arc so its own tangent point/direction
// exactly continues from the previous leg's end (each later arc's own
// center is wherever that arc's own `inside_radii[i]` places it, not a
// shared fixed center the way Bend()'s own single arc has one) - then
// assembles one closed 2D profile (outer boundary, then inner boundary
// reversed, exactly like Bend()'s own profile, just with N legs/arcs each
// instead of 2/1) and extrudes it by `width` via ONE Brep::Extrude() call.
// Reduces EXACTLY to Bend(leg_lengths[0], leg_lengths[1], width,
// thickness, inside_radii[0], bend_angles_degrees[0]) for a single bend
// (N == 1) - the identical profile, not just a similar one - cross-checked
// directly (TestMultiBendReducesToBendForASingleBend, tests/test_basic.cpp).
//
// Throws std::invalid_argument if `leg_lengths` has fewer than 2 entries
// (at least one bend is required - a flat, unbent sheet is out of this
// function's own scope), if `bend_angles_degrees`/`inside_radii` don't
// each have exactly `leg_lengths.size() - 1` entries, for any non-positive
// leg length/width/thickness/inside_radius, for any
// `bend_angles_degrees` entry not in (0, 180) - plus whatever
// Brep::Extrude() itself throws if the assembled chain's own profile isn't
// star-shaped (correspondingly easier to exceed with more/sharper bends
// than Bend()'s own single-bend case).
Brep MultiBend(const std::vector<double>& leg_lengths, const std::vector<double>& bend_angles_degrees,
               const std::vector<double>& inside_radii, double width, double thickness);

// Flattens a Bend()-built part back to its own flat pattern - the inverse
// direction of Bend() above, and this kernel's answer to the parity-map
// "Sheet-metal features" item's own disclosed "Unfold" named sub-feature
// (previously Bend()/MultiBend() only built the bent 3D part; nothing
// produced the flat blank a press brake actually starts from).
//
// Takes the IDENTICAL parameters Bend() itself takes (plus `k_factor`,
// passed straight through to BendAllowance() above), rather than
// reverse-engineering an arbitrary Brep: Bend() builds its own solid
// directly from these parameters with no intermediate flat-pattern
// representation to invert, so this recomputes the flat length from the
// same inputs instead. The result is a single flat rectangular plate -
// `thickness` thick, `width` wide, and `leg1_length + leg2_length +
// BendAllowance(thickness, inside_radius, bend_angle_degrees, k_factor)`
// long (`BendAllowance()`'s own doc comment above derives that length) -
// built the same way Bend() itself is, via Brep::Extrude() of a
// rectangular profile, NOT Brep::BoxWelded(): a FromPlanarFaces()-built
// BoxWelded() genuinely welds its own topology but trims each face with a
// real ON_BrepLoop, so Brep::FaceCoversWholeDomain() (brep.cpp) correctly
// reports it as trimmed and Brep::Volume()'s own exact per-face
// integration refuses it outright ("face 0 is trimmed") - tried first and
// confirmed to fail this way, dino8_scratch_test, on a bare
// `Brep::BoxWelded(0,0,0,2,3,4)` with no sheet-metal code involved at
// all. Extrude()'s own faces get a real ON_Brep loop running along the
// surface's own boundary instead (`raw().FaceIsSurface()`), which
// Volume() DOES integrate directly - exact to float precision for this
// rectangular, axis-aligned case.
//
// At `k_factor == 0.5`, this is not just an industry approximation:
// Bend()'s own doc comment proves its TRUE geometric volume already uses
// the identical mid-plane radius (`inside_radius + thickness/2`)
// BendAllowance() reaches at `k_factor == 0.5` - so
// `UnfoldBend(..., 0.5).Volume()` matches Bend(...)'s own exact Pappus
// volume bit-for-bit, not just approximately - verified directly,
// TestUnfoldBendAtHalfKFactorExactlyMatchesBendPappusVolume. Any other
// `k_factor` is the standard sheet-metal approximation (deliberately NOT
// volume-exact - a K-factor other than 0.5 models where a real press
// actually puts the neutral axis, not where the material's own geometric
// mid-plane sits).
//
// Throws std::invalid_argument for a non-positive leg1_length/leg2_length/
// width, or whatever BendAllowance() itself throws for a non-positive
// thickness/inside_radius, a bend_angle_degrees out of (0, 180), or a
// k_factor out of [0, 1].
Brep UnfoldBend(double leg1_length, double leg2_length, double width, double thickness, double inside_radius,
                double bend_angle_degrees, double k_factor = 0.44);

// The MultiBend() generalization of UnfoldBend() above, exactly the same
// "flat length = sum of legs + sum of each bend's own BendAllowance()"
// construction extended from one bend to a chain of N - closing the same
// "Unfold" sub-feature for a real multi-bend flat pattern (a U-channel or
// hat-channel's own flat blank), not just a single bend's.
//
// Takes the identical `leg_lengths`/`bend_angles_degrees`/`inside_radii`
// parameters MultiBend() itself takes (plus `width`/`thickness`/
// `k_factor`); the result is, again, a single flat Extrude()-built plate
// (see UnfoldBend()'s own doc comment for why Brep::BoxWelded() is
// deliberately NOT used here) of `leg_lengths` summed plus one
// BendAllowance() call per bend
// (each at its OWN `inside_radii[i]`/`bend_angles_degrees[i]`, a genuine
// multi-radius flat pattern, not a single shared radius).
//
// At `k_factor == 0.5`, exactly like UnfoldBend() above, this matches
// MultiBend(...)'s own exact multi-radius Pappus volume (leg lengths
// summed plus each bend's own `bend_angle_radians*(inside_radii[i] +
// thickness/2)`) bit-for-bit - verified directly against the identical
// U-channel/hat-channel fixtures
// TestMultiBendUChannelAndHatChannelMatchPappusClosedForm already uses,
// TestUnfoldMultiBendAtHalfKFactorExactlyMatchesMultiBendPappusVolume.
//
// Throws std::invalid_argument if `bend_angles_degrees` is empty, if
// `leg_lengths`/`inside_radii` don't each have the same counts
// MultiBend() itself requires, for a non-positive leg length or width, or
// whatever BendAllowance() itself throws for any one bend's own
// thickness/inside_radius/bend_angle_degrees/k_factor.
Brep UnfoldMultiBend(const std::vector<double>& leg_lengths, const std::vector<double>& bend_angles_degrees,
                      const std::vector<double>& inside_radii, double width, double thickness,
                      double k_factor = 0.44);

}  // namespace dino8::kernel
