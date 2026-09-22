// General boundary-evaluation boolean engine - see boolean_general.h for
// the scope/limitations summary. This file is the actual algorithm:
//
//   1. Every face of A is tested against every face of B (bbox-pruned)
//      via IntersectFaces() (dino8/kernel/surface_intersect.h), which
//      already restricts the resulting curves to each face's own trimmed
//      region and already hands back matching 2D pcurves on BOTH faces'
//      own (u, v) space - no projection needed.
//   2. Each face's own trim-loop boundary (its real ON_BrepLoop if it has
//      one, else the surface's own full parameter-domain rectangle for an
//      untrimmed face - Box()/Sphere()) is split by the intersection
//      curves gathered for it: a CLOSED curve (entirely interior to the
//      trim - IntersectionCurve::closed) becomes a hole in the untouched
//      fragment plus a new, separate single-loop fragment for its own
//      interior; an OPEN curve (both ends on the trim boundary, since
//      IntersectFaces() already clips/splits there) is spliced into the
//      trim boundary, bisecting it into two fragments at the two
//      insertion points.
//   3. Every fragment is classified in/out of the OTHER solid by picking
//      one interior (u, v) point, mapping it to 3D, and ray-casting
//      against every face of the other solid via IntersectCurveSurface()
//      (the general CSX, not a hand-solved ray/plane or ray/cylinder
//      formula) - the same parity-counting idea ClassifyPointVsSolid in
//      boolean.cpp already uses, generalized to arbitrary faces.
//   4. The kept fragments (Union: both sides' OUT pieces; Intersection:
//      both sides' IN pieces; Difference: A's OUT pieces + B's IN pieces
//      with B's orientation flipped) are reassembled into one ON_Brep
//      with genuine topology: fragment boundary points are welded into
//      shared ON_BrepVertex ids by 3D coincidence (the same identity
//      principle Brep::FromMixedFaces()'s own VertexWelder uses), and
//      since a fragment pair on either side of a shared intersection
//      curve is built from the SAME IntersectionCurve::points array (only
//      the (u, v) differs between the A-side and B-side pcurve), the two
//      sides' welded points coincide exactly - so the shared cut becomes
//      one literal shared ON_BrepEdge, not two independently-approximated
//      curves that merely sit close together.
//
// Edges here are dense straight-segment polylines between consecutive
// (already densely sampled, by IntersectFaces()'s own tessellation-seeded
// refinement, and by FaceBoundaryLoop()'s own sampling of a real trim or
// of an untrimmed domain rectangle) fragment-boundary points - the same
// "genuine multi-point polyline, not a single exact analytic curve"
// approach brep.h's own FromMixedFaces()-built notch edges already use
// for a boundary with no simple closed form, applied here uniformly
// (every edge, not just notches) since a general ON_Surface face has no
// guaranteed isocurve family to fall back on the way a cylinder's cap
// does. This is a real, disclosed precision tradeoff (see
// boolean_general.h): the assembled B-rep's edges are polygonal
// approximations of the true intersection curves, accurate to
// IntersectOptions::tolerance, not exact to floating point.
// FORMERLY-CONFIRMED GAP, NOW ROOT-CAUSED AND FIXED (the "IntersectFaces()
// returns ZERO curves for a box's flat face vs a cylinder's periodic wall"
// symptom originally documented here): it was never really "zero curves"
// in IntersectFaces() itself - it was the correctly-found closed loop
// (the box plane's own circular cross-section of the cylinder wall)
// getting silently corrupted/discarded downstream, by THREE independent,
// now-fixed bugs, each confirmed by direct before/after tracing:
//   (a) surface_intersect.cpp's own SplitAtSeams() unconditionally
//       stamped `closed = false` on any curve it touched at all, even a
//       curve whose ONLY "seam crossing" is its own closing wraparound
//       edge (a full loop that goes once around a periodic direction and
//       crosses that direction's seam exactly once, right where it
//       closes) - turning a genuinely closed loop into a bogus "open" arc
//       whose two "ends" are not on any real trim boundary at all, so
//       SplitFaceLoop() (below) correctly refused to use it and dropped
//       it. Fixed: such a curve is now recognized and kept closed.
//   (b) PointInPolygon() (surface_intersect.cpp) had no boundary-inclusive
//       tolerance: a curve point Newton-refined to sit EXACTLY on a full
//       (angle == 2*pi) CylindricalFace's own trim rectangle's own right-
//       hand edge (u == u_max, the seam) could test as marginally
//       "outside" that trim from ordinary floating-point residue, and
//       IntersectFaces()'s own point-vs-trim clipping (this file's own
//       caller) then rips the whole closed loop open at that one spurious
//       point. Fixed: a point within a scale-relative epsilon of any
//       trim-polygon edge now counts as inside.
//   (c) even with (a)/(b) fixed and IntersectFaces() correctly returning
//       the loop with IntersectionCurve::closed == true, THIS file's own
//       Chain-building loop (right below) never consulted that flag - it
//       re-derives open/closed purely by comparing a Chain's own stored
//       first/last 3D points, which only agree for a chain that repeats
//       its own closing point, the convention IntersectFaces() does NOT
//       use (a closed IntersectionCurve stores N distinct points with an
//       IMPLICIT wrap, exactly like every other closed curve in this
//       module). Fixed: a Chain built from an already-closed
//       IntersectionCurve now re-appends its own first point so this
//       file's own closed-vs-open test agrees.
// Verified directly (DINO8_BOOL_DEBUG=1 on the box-fully-pierced-by-a-
// cylinder fixture in scratch_test.cpp): the box's flat faces now DO
// split against the cylinder wall (frags > 1, a real hole/interior
// fragment pair appears where the circle is entirely interior to the
// flat face), where every one of them previously reported frags=1.
//
// FORMERLY-DISCLOSED "REMAINING GAP" ABOVE THIS PARAGRAPH, NOW ALSO FIXED:
// for the SAME fixture (a flat cutting plane exactly PERPENDICULAR to the
// cylinder's own axis, so the plane's cross-section of the wall is the
// cylinder's ENTIRE circumference, not a transversal arc), the assembled
// polyline could still self-cross / fold back on itself at a few points,
// which corrupted the downstream fragment polygon (Difference/
// Intersection threw "an edge is claimed by 3 or more fragment loops" or
// NurbsSurface::TessellateGridClippedExact's own "trim_polygon must be
// simple" check) and left the Union volume measurably wrong. Root-caused
// to THREE further, independent bugs, all specific to a wrap-swept
// periodic curve, each confirmed by direct before/after tracing:
//   (d) SplitFaceLoop() (below) had no notion of a "wrap-cut": a 3D-closed
//       chain that is really a single full sweep of one of a face's own
//       periodic directions (this exact circle, on the cylinder wall's own
//       side of the pair) was always treated as an island - holed out of
//       the untouched fragment plus spun off as its own tiny interior
//       fragment - rather than as a cut that bisects the wall into the two
//       bands above/below it. Two circles (this fixture's box top AND
//       bottom faces both cross the wall) holed the SAME single fragment
//       twice, producing overlapping, self-intersecting nonsense once
//       flattened. Fixed: SplitPeriodicWrapChain() detects a closed chain
//       with exactly one seam crossing (the same signal
//       surface_intersect.cpp's own SplitAtSeams() already uses) and
//       re-cuts it into an ordinary open chain whose two new ends land on
//       the face's own two SEAM sides of its trim boundary (see
//       FaceBoundaryLoop()'s own doc comment on why a full-sweep periodic
//       face's own boundary already has both seam sides as distinct
//       edges), then splices it exactly like any other open chain.
//   (e) That wrap-cut's own two new ends are ordinary curve samples near
//       the seam, not points refined to sit exactly on it (up to one mesh
//       cell's worth of parameter off) - fine for SplitFaceLoop()'s own
//       splice-tolerance check once (d) also snaps their own (u, v) onto
//       the exact seam value, but this face's own reconstructed edge/trim
//       pair then had a genuine 3D gap between where the trim curve's
//       endpoint evaluates on the surface and where the edge curve's own
//       endpoint actually sits - exactly what later failed
//       ON_Brep::IsValid()'s own trim-vs-edge distance check ("Distance
//       from start of ON_Brep.m_T[...] to 3d edge is 0.12..."). Fixed by
//       NOT moving the shared point (needed, unchanged, for cross-face
//       vertex welding against the box's own copy of this same physical
//       point) at all: a brand-new vertex, genuinely on the seam (exact
//       (u, v) AND a freshly surface-evaluated 3D point that matches it),
//       is spliced in one step further out instead, adding one small,
//       wholly-this-face-only extra facet.
//   (f) FinishCurve()'s (surface_intersect.cpp) adaptive Newton
//       subdivision could still jump a segment's own inserted midpoint to
//       a distant, equally-valid point on this SAME circle instead of the
//       geometrically nearest one - RefineSurfaceSurfacePoint()'s
//       3-equation/4-unknown Newton system is only weakly damped along a
//       curve's own tangent direction (every point on this specific
//       circle is an equally valid zero-residual solution, since the
//       cutting plane is exactly perpendicular to the cylinder's axis
//       everywhere along it), a real, reproduced (not theorized) defect
//       confirmed via direct seed/refined-point tracing. Fixed with two
//       independent guards on that one insertion, both scaled to the
//       segment's own local span rather than any fixed constant: reject
//       an inserted point farther from the cubic-fit midpoint than the
//       segment's own chord length, AND reject one whose own (u, v) in
//       EITHER surface's chart falls outside the bracket its two
//       endpoints already span (plus modest curvature slack) - the second
//       guard catches small-amplitude back-and-forth jitter the first,
//       alone, still let through.
// A fourth, unrelated bug was found and fixed alongside these: (g)
// FaceBoundaryLoop() decided how densely to sample each trim EDGE purely
// from the 2D (u, v) trim curve's own linearity - true for every edge of a
// periodic surface's own full-sweep trim rectangle, including the two that
// run ALONG the periodic direction itself (e.g. a cylindrical wall's own
// full-circle rim at constant height). Those two are NOT straight in 3D;
// sampling just 2 points for one (the fast path for a genuinely straight
// edge) collapsed a whole rim circle down to a single chord, which then
// welded into a degenerate 2-vertex "digon" edge once a kept fragment
// reused it verbatim - confirmed as the cause of a SEPARATE
// ON_Brep::IsValid() failure (a stale seam-iso-flag mismatch,
// "ON_Brep.m_T[...].m_iso = S_iso but matching seam ... != N_iso") on this
// fixture's own Union result specifically (Intersection/Difference don't
// keep the wall's own untouched top/bottom rim bands that trip this).
// Fixed by also checking the edge's own 3D image is genuinely straight
// (its true midpoint sits on its own end-to-end chord) before trusting the
// 2-point fast path.
//
// VERIFIED (dino8-kernel/tests/test_basic.cpp's own
// TestBooleanCombineGeneralBoxCylinder, mirroring
// TestBooleanCombineGeneralBoxBox's own rigor): box+cylinder Union,
// Intersection, and Difference on the box-fully-pierced-by-a-perpendicular-
// cylinder fixture are now ALL ON_Brep::IsValid() and tessellate to their
// exact closed-form volumes within a tessellation-scaled tolerance -
// BooleanCombineGeneral's first proven curved-operand case, not just a
// planar-only one.
//
// FORMERLY "STILL NOT FIXED" ABOVE THIS PARAGRAPH, NOW ROOT-CAUSED AND
// FIXED: sphere+box (the fixture in scratch_test.cpp and
// TestBooleanCombineGeneralSphereBox: a radius-2 sphere at the origin vs a
// box with one corner AT the sphere's centre and its three faces on the
// coordinate planes, so the intersection is exactly one octant). The
// earlier diagnosis recorded here ("three open arcs meeting at three
// corners, no periodicity involved, so the stitch/corner assembly must be
// producing the wrong topology") was half right: it WAS a topology
// problem, but it was entirely about the sphere's own (u, v) chart, and
// nothing about the stitching itself. In ON_Sphere::GetNurbForm()'s chart
// (u = longitude, seam at +x; v = latitude, poles at v = +-pi/2) this
// fixture is the worst case there is: the y == 0 plane's own arc lies
// EXACTLY on the u == 0 seam meridian, and both it and the x == 0 plane's
// arc end at the degenerate north pole. Three independent bugs, each
// confirmed by direct before/after tracing (DINO8_BOOL_DEBUG=1 plus
// temporary per-stage dumps inside IntersectSurfaces()/IntersectFaces(),
// since removed):
//   (h) surface_intersect.cpp's own FaceContainsUV() tested an UNTRIMMED
//       face's domain with ON_Interval::Includes(t, true) - whose second
//       argument is `bTestOpenInterval`, i.e. min < t < max - and so
//       rejected every sample sitting exactly on the domain boundary. The
//       mesh seeding was complete (traced: the five seed chains covered
//       the whole seam arc), but RefineSurfaceSurfacePoint()'s Newton
//       solve clamps (u, v) to the domain, so a seam sample lands on
//       u == 0 bit-exactly - and IntersectFaces()'s own clip then threw
//       those away (while keeping neighbours that happened to round to
//       u == 1e-17), ripping the seam arc into pieces with real gaps and
//       dropping the equator arc's own u == 0 endpoint. THIS was the
//       "far too small" Intersection: with the arcs never closing, the
//       sphere's only In fragment was a sliver. Fixed: closed-interval
//       test (the seam and poles ARE part of an untrimmed face).
//   (i) With (h) fixed the three arcs stitch into one 3D-closed chain -
//       which SplitFaceLoop() then holed out as an interior island. But in
//       (u, v) it is NOT an island: it is the corner square [0, pi/2]^2
//       of the sphere's domain rectangle, running along the rectangle's
//       own seam side and pole side for their full length. The "hole"
//       overlapped the outer loop's own seam edge (IsValid() failure on
//       Union/Difference, whose volumes were short by the misassigned
//       surface). Fixed by CutChainAtDomainBoundary() (see its doc
//       comment): chain points on a seam/singular side of an untrimmed
//       face are snapped onto it in (u, v) (their shared 3D point is
//       deliberately left alone), the on-boundary runs REPLACE the loop's
//       own samples over that span on both seam twins (so the seam stays
//       one welded edge and the box's own copy of the arc becomes a
//       literal shared edge), and the rest of the chain is spliced as an
//       ordinary open chain. Alongside: StitchChains() dropped one of the
//       two (u, v) copies of the pole junction (same 3D point, u = pi/2
//       vs u ~ 0.26), drawing a bogus diagonal near the pole; both copies
//       are now kept (AppendStitched()).
//   (j) Assembly: the loop samples along a pole line all weld to one
//       vertex, and CollapseDuplicateVids() kept only the FIRST of them,
//       so the trim leaving the south pole up the u_max seam side started
//       at the u_min corner's (u, v) - a diagonal 2D line on a seam-type
//       trim, which IsValid() rightly rejects ("m_type = seam but m_iso is
//       not N/E/W/S_iso"). Pre-existing for ANY sphere result, this file
//       just never had a valid sphere case to show it. Fixed the proper
//       way: the run's first AND last (u, v) are kept and BuildLoop()
//       bridges them with a genuine ON_Brep::NewSingularTrim() along the
//       pole line, so every real trim keeps its exact iso (u, v). Restricted
//       to genuinely singular sides (SingularSideIso()), so a same-vertex
//       pair that merely differs by Newton noise still collapses to one
//       point as before - keeping both there left a 2D gap that briefly
//       regressed box+box's own IsValid() during this work.
// VERIFIED (TestBooleanCombineGeneralSphereBox): Union, Intersection and
// both Difference orders are ON_Brep::IsValid() and tessellate to their
// closed-form volumes (octant = (4/3*pi*r^3)/8) with an error that falls
// ~4x per doubling of the tessellation (Intersection: 0.110 / 0.029 /
// 0.0074 at n = 16 / 32 / 64), i.e. convergent tessellation error of the
// same order as a plain sphere's own. box+box and box+cylinder unchanged.
//
// ALSO CONFIRMED, separately, while verifying the above (not introduced by
// this session, though PARTIALLY fixed by it - see below): plain
// Brep::TessellateToClosedMesh()/TessellateToClosedMeshConforming() on this
// engine's own results are not Mesh::IsClosedManifold() at any resolution
// tried, on box+box, box+cylinder, or sphere+box - this engine's edges are
// dense straight-segment polylines (see the top of this file), and neither
// tessellator has ever matched a general trimmed face's own polyline
// boundary the way TessellateConforming()'s existing analytic-curve/plain-
// quad passes match theirs, so two faces sharing one such edge can sample
// it at different densities and leave T-junctions.
//
// PARTIALLY FIXED: a new, purely additive, opt-in function scoped ONLY to
// this engine's own results, TessellateGeneralBooleanClosedMesh() (see
// boolean_general.h's own doc comment for the exact algorithm - it calls
// the unmodified, shared Brep::Tessellate() and then re-triangulates any
// boundary edge with an un-partnered vertex sitting on it as a fan through
// that vertex, so adjacent faces' boundary vertex sets agree before
// welding), makes box+box GENUINELY Mesh::IsClosedManifold() - boundary and
// non-manifold edge counts drop to exactly zero on all three ops, confirmed
// by direct measurement (dino8-kernel/tests/scratch_test.cpp). box+cylinder
// and sphere+box are substantially IMPROVED by the same function (e.g.
// sphere+box Difference: 40 non-manifold / 167 boundary edges down to 4 /
// 122) but NOT yet fully closed - the remaining gap is on this engine's
// curved-face polyline edges specifically (a fan through one extra vertex
// is not always enough to reconcile two independently-sampled polylines
// that both approximate, rather than lie exactly on, the same true curve),
// still a separate, open piece of work. TestBooleanCombineGeneralBoxBox
// asserts IsClosedManifold() via this new function; BoxCylinder and
// SphereBox deliberately do not, since it is not yet true for them.
//
// SESSION: dino8-kernel/tests/general_boolean_sweep.cpp's own 76-case
// measurement sweep (19 cases x 4 ops), ranked-diagnosis follow-up. Went
// from 56/76 OK to 60/76 OK. Three root causes found and fixed, all in the
// wrap-cut/adaptive-refinement machinery this file and surface_intersect.cpp
// share, none of them the level-perpendicular-plane case the earlier
// box+cylinder work above already covers:
//   (k) SplitPeriodicWrapChain()'s own two new seam-side vertices
//       (front_seam/back_seam) each took the surface's OTHER ("cross")
//       coordinate - e.g. a cylinder wall's own axial height - from their
//       OWN nearby original chain sample, independently. For a wrap-cut
//       that is geometrically level (sphere+cylinder, sweep case 13: the
//       intersection is exactly two circles of constant height) those two
//       independent samples are only supposed to agree to within ordinary
//       Newton-refinement noise - normally invisible, but confirmed by
//       direct tracing to differ by ~1.05e-6 here, just ABOVE this file's
//       own kWeldTol (1e-6), so the two vertices that should have welded
//       into one shared seam-edge endpoint instead stayed two, producing a
//       genuine zero-length-2D-trim ON_Brep::IsValid() failure. Fixed:
//       SplitPeriodicWrapChain() now computes ONE shared cross-direction
//       value, linearly interpolated between the two samples that actually
//       straddle the seam crossing (the same interpolate-then-pin idea
//       surface_intersect.cpp's own SeamCrossing() uses for a real
//       surface-pair seam crossing, specialized to this single
//       already-known polyline), and uses it for BOTH new vertices. This
//       also directly benefits a SLOPED or SKEW wrap-cut (sweep cases 03,
//       08), where the two straddling samples' cross-direction values
//       genuinely differ geometrically, not just by noise - interpolating
//       is then the correct thing to do, not merely noise-cancelling.
//   (l) A further, separate degeneracy on the SAME fixture: the two
//       samples straddling a seam crossing are not always an ordinary pair
//       merely NEAR the seam - when the upstream discretization happens to
//       seed a sample exactly AT one domain end (NewtonSolve()'s own hard
//       Clamp() to the domain, confirmed to fire here), inserting a
//       brand-new vertex right next to an already-exact-seam sample gave
//       the new point the identical (u, v) as that original sample (since
//       its cross-direction value was taken from - or, after (k), blended
//       heavily toward - that very sample) but a different `p` (the
//       original sample's own small SSX residual vs. the new point's exact
//       analytic evaluation): a second, independent source of the same
//       zero-length-2D-trim failure. Fixed by reusing the existing
//       straddling sample IN PLACE (only tidying its own (u, v), `p`
//       deliberately left untouched, mirroring CutChainAtDomainBoundary's
//       own already-proven approach) instead of inserting a redundant
//       vertex negligibly close to it, whenever that sample is already at
//       the seam to within Newton's own convergence.
//   (m) FinishCurve()'s (surface_intersect.cpp) adaptive Newton
//       subdivision inserts at most one new point per segment, checked
//       only against that segment's own two endpoints (a bracket with 25%
//       slack, to tolerate real curvature) - so two ADJACENT segments,
//       each independently refined and each individually passing its own
//       check, could still produce a small back-and-forth reversal at the
//       shared point between them (segment 1 inserts a point close to its
//       start; segment 2, now anchored there, inserts ITS OWN point that
//       overshoots backward past segment 1's own insertion - bracketed
//       fine against segment 2's own endpoints, but a clear local reversal
//       once assembled). Confirmed by direct tracing on a sloped cylinder
//       (case 03) and a cone (case 15): the resulting polyline folds back
//       on itself, producing a self-intersecting 2D trim loop
//       ("trim_polygon must be simple") once flattened - a tangent-
//       degenerate cross-section (the cone/skew analogue of the earlier
//       perpendicular-cylinder tangent case) makes this measurably more
//       likely than on a plain circle, though the mechanism itself is not
//       specific to periodicity at all. Fixed with two additions: an
//       insertion-time guard rejecting a new point that itself moves
//       backward from its own segment's start point (tight slack, just
//       enough for Newton noise - independent of the existing 25% bracket,
//       which alone does not catch this), and a final cleanup pass that
//       drops any ALREADY-ASSEMBLED point found to reverse direction
//       relative to its own two immediate neighbors (using the same
//       generous, curvature-tolerant 25% bracket the ordinary insertion
//       check trusts, so genuine curvature is not disturbed) - needed
//       because the insertion-time guard alone cannot see the
//       cross-segment case (m) describes.
// VERIFIED: sweep case 03 (box+cyl OBLIQUE axis piercing) goes from 3/4 OK
// (B-A threw "trim_polygon must be simple") to 4/4 OK; case 15 (box+cone
// perpendicular piercing) goes from 1/4 OK (Union/B-A threw, Intersection
// measured NaN via the same throw, only A-B was already OK) to 4/4 OK.
// Both are also asserted directly in dino8-kernel/tests/test_basic.cpp
// (TestBooleanCombineGeneralObliqueCylinder, TestBooleanCombineGeneralBoxCone
// - IsValid() plus closed-form volume within a tessellation-scaled
// tolerance that is itself checked to shrink under a doubled tessellation,
// same template as TestBooleanCombineGeneralBoxCylinder/SphereBox above).
// The full dino8_kernel_tests suite (1525 checks, unrelated to this sweep)
// remains 100% passing, confirming (m)'s change to FinishCurve - shared
// with the older, separately-tested BooleanCombineMixed engine in
// boolean.cpp only in principle, since that file does not actually
// #include surface_intersect.h at all in this codebase - did not disturb
// anything.
//
// SESSION 2: sphere-seam follow-up, targeting sweep cases 12 and 13
// specifically (both involve a sphere operand; both were suspected,
// entering this session, to share a root cause in CutChainAtDomainBoundary
// - confirmed true for 13, confirmed FALSE for 12, by direct tracing of
// each independently, per below). Went from 60/76 OK to 64/76 OK.
//   (n) Sweep case 13 (sphere+cyl axis through centre piercing), the
//       "STILL NOT FIXED" item this section used to carry (its own former
//       text: "Distance from start of ON_Brep.m_T[0] to 3d edge is
//       0.0143..." on Union/A-B/B-A, "an edge is claimed by 3 or more
//       fragment loops" on Intersection) - ROOT-CAUSED AND FIXED, and the
//       prior session's own speculative diagnosis (an unweighted,
//       un-interpolated disagreement between two on-seam "runs", the (k)
//       pattern applied to CutChainAtDomainBoundary) was WRONG: direct
//       tracing (temporary per-point dumps inside CutChainAtDomainBoundary,
//       since removed) showed the two level circles this fixture's own
//       sphere face sees (at z = +-sqrt(R^2-r^2), each a full sweep of the
//       sphere's own longitude that closes up right at its u == 0/2*pi
//       seam - structurally a wrap-cut, but funneled through
//       CutChainAtDomainBoundary rather than SplitPeriodicWrapChain because
//       the sphere is untrimmed) each produce a run of exactly TWO
//       consecutive on-seam samples per seam side, not one: the genuine
//       seam-crossing sample itself (3D distance to its own snapped (u, v)
//       point measured bit-exactly 0) AND its own ordinary neighbouring
//       sample one mesh cell farther around the circle (measured 3D
//       distance ~0.0143 - the EXACT value ON_Brep::IsValid() later
//       complained about). That second, spurious sample only qualified as
//       "on the seam" at all because the on-boundary tolerance
//       build_frags() passed to CutChainAtDomainBoundary was
//       std::max(stitch_tol, opt.tolerance) - stitch_tol (~20x opt.tolerance,
//       meant for reconciling independently-refined face-pair SSX curve
//       endpoints elsewhere) dominates and is roughly 20x looser than the
//       ~0.0143 gap, while opt.tolerance alone is roughly 15x TIGHTER than
//       that gap - even though this call site's own pre-existing comment
//       already said the intent was "the on-boundary tolerance is the
//       SSX's own accuracy," the code passed the much looser value instead.
//       With the spurious neighbour wrongly included, CutChainAtDomain-
//       Boundary spliced it into the loop as its own extra vertex, whose
//       own (u, v) sits at the exact seam value but whose own `p` (left
//       deliberately unmoved, by design) is ~0.0143 away from where that
//       (u, v) actually evaluates - precisely the trim-vs-edge distance
//       IsValid() flags. Fixed with a one-line change to the tolerance
//       actually passed (opt.tolerance, not std::max(stitch_tol,
//       opt.tolerance)) - aligning the code with what its own comment
//       already claimed, not a new invented constant. This correctly
//       leaves each run at exactly one genuine seam-crossing sample per
//       side (an isolated point, per this function's own "isolated
//       near-seam sample... left entirely alone" rule, EXCEPT it is not
//       isolated here: its cyclic neighbour across the array's own
//       wraparound is the OTHER seam side's genuine crossing sample, so
//       both still count as "in a run" and get spliced - just with no
//       spurious extra vertex now, and the two genuine crossing samples
//       already agree to float precision, needing no (k)-style
//       interpolation at all). VERIFIED: sweep case 13 goes from 0/4 OK
//       (Union/A-B/B-A INVALID, Intersection EXCEPTION) to 4/4 OK, with
//       ZERO change to any of the other 75 cases (full before/after verdict
//       diff). New TestBooleanCombineGeneralSphereCylinderThroughCentre
//       (test_basic.cpp) asserts IsValid() and closed-form volume
//       (4/3*pi*(R^3-(R^2-r^2)^1.5)) for all four ops plus a tessellation-
//       doubling convergence check, mirroring the existing sphere/cylinder
//       general-boolean tests' own template.
//   Sweep case 12 (sphere+sphere overlapping, unequal radii) - ROOT-CAUSED,
//   NOT FIXED, genuinely independent of (n) as suspected (confirmed by
//   direct tracing, not assumed): Union and B-A measure short by ~1.23 and
//   ~1.02 (out of ~42.6 and ~9.06); Intersection and A-B are already OK.
//   Direct per-op fragment tracing (DINO8_BOOL_DEBUG=1) shows the lens
//   circle DOES cross sphere A's own seam (A's centre sits on the ray the
//   B-centre offset defines, so A's own u == 0 meridian passes through the
//   lens plane) - handled correctly by (n)'s own fixed code path, confirmed
//   by A's own fragments (Out/2xIn, no holes) being numerically exact in
//   every op that uses them (A-B, which needs A's Out, is OK). Sphere B's
//   copy of the SAME circle does NOT cross B's own seam (B's offset keeps
//   its own meridian on the far side) and is instead handled as an
//   ordinary CLOSED chain producing an ordinary hole - the ordinary,
//   long-proven "closed chain -> hole in an untouched fragment" path
//   (item 2 at the top of this file), not any seam/pole code at all. That
//   fragment (the whole sphere B domain rectangle as outer, with the lens
//   circle as one interior hole) is EXACTLY the one used by both wrong ops
//   (Union: A's Out + B's Out; B-A: B's Out + A's In) and never by the two
//   correct ones (Intersection/A-B, which only ever use B's In, the hole
//   polygon itself with no separate outer). Isolated the defect below
//   BooleanCombineGeneral entirely, in the shared, general-purpose
//   tessellation pipeline it hands its result to (confirmed by a standalone
//   probe measuring Mesh::Area() per assembled face against the closed-form
//   spherical-cap arithmetic): brep.cpp's ResolveFace() sets
//   `exact_clip = false` whenever a face carries ANY hole loop (its own
//   comment: TessellateConforming() cannot clip a hole at all, so a holed
//   face is routed to Brep::Tessellate()'s ordinary TessellateGrid() path
//   instead of NurbsSurface::TessellateGridClippedExact()), and
//   TessellateGrid()'s own whole-cell trimming (surface.cpp's
//   TessellateFromValues - keep a grid cell only when ALL FOUR of its
//   corners test inside outer-minus-holes, drop it whole otherwise) is a
//   materially cruder approximation than the exact-clip path: measured
//   directly on this fixture's own assembled B-side face, sphere B's own
//   Out annulus (full domain minus the lens hole) tessellates to 18.68 sq
//   units at TessellateToClosedMesh(64, 256) and 17.96 at the sweep's own
//   (32, 128), against a closed-form (analytic cap subtracted from the
//   full sphere) expectation of ~19.44 - a ~4-8% area deficit, roughly two
//   orders of magnitude worse than the ~0.05% error an EXACT-clip fragment
//   of comparable size shows at the same resolution (confirmed: sphere A's
//   own holeless Out fragment, same fixture, same resolution, measures
//   43.176 against a closed-form 43.197). This is not a defect
//   BooleanCombineGeneral introduces - the whole-cell hole-trimming
//   tradeoff is pre-existing, general kernel behaviour (see brep.cpp's own
//   doc comment on ResolveFace(), which already discloses it) - this
//   fixture is simply the first case anywhere in the kernel's own tests to
//   put an ordinary, non-seam-touching hole on a SMALL, fully-untrimmed,
//   doubly-periodic/singular (sphere) domain, where the O(hole perimeter x
//   cell size) staircase error the whole-cell method inherently carries is
//   a large fraction of the whole face's own area, rather than a
//   negligible fraction of a much bigger flat face as in every other
//   holed-face case this kernel already exercises (e.g. sweep case 01's
//   box-face-with-a-circular-hole). NOT FIXED this session: the
//   correct general fix (teaching TessellateGridClippedExact-style exact
//   clipping to also subtract one or more hole polygons per cell, or
//   equivalently bridging an outer+hole loop pair into one single "keyhole"
//   polygon so the EXISTING exact-clip path can be used unmodified) is
//   shared, broadly-relied-on tessellation code (surface.cpp/brep.cpp, used
//   far beyond BooleanCombineGeneral), and a same-session attempt at the
//   keyhole-bridging approach (entirely local to this file's own KeptFace
//   construction, so it would NOT have touched shared code) was tried and
//   ABANDONED after direct measurement: even with the bridge's own winding
//   corrected (the hole must be walked in the OPPOSITE direction from the
//   outer loop so the merged polygon's net signed area is outer - hole, not
//   their sum - confirmed via SignedArea()), the resulting single polygon's
//   own EXACT-clip tessellation came back wildly wrong (213 sq units on a
//   ~28 sq unit sphere) despite IsSimplePolygon() reporting it as simple -
//   the shared concave polygon-clipping path (ClipPolygon/EarClipTriangulate
//   in surface.cpp) is not robust to the zero-width, exactly-self-touching
//   "slit" a naive bridge produces (this file's own TessellateGridClippedExact
//   already has an unrelated "nudge points off grid lines" workaround for a
//   different exact-degeneracy sensitivity in the very same concave path,
//   corroborating that this is a real, known-fragile area, not a fluke of
//   this one fixture). Making the bridge itself robust (a small, carefully-
//   directed offset, the standard "simulation of simplicity" fix for this
//   exact class of degeneracy) is a real, well-understood next step, but
//   verifying it doesn't regress the concave-clip path's other, already-
//   proven callers is more work than this session had time to do safely -
//   left for a future session's own direct attempt, now with this
//   diagnosis and the two failed/succeeded experiments above as a starting
//   point rather than a fresh trace.
//
// SESSION 3: sweep case 07 (cyl+cyl perpendicular UNEQUAL radii) follow-up.
// Went from 64/76 OK to 68/76 OK - case 07 fully fixed (4/4), and, as a
// direct consequence of the same fix, case 12 (sphere+sphere, the "ROOT-
// CAUSED, NOT FIXED" item two sessions above) is ALSO now fixed (4/4),
// superseding that entry's own "NOT FIXED" conclusion - left in place
// above, unedited, as the accurate record of what session 2 itself found
// and did not fix, since this session's fix was not the "keyhole-bridging"
// experiment session 2 tried and abandoned for THAT fixture specifically
// (see below for why the same idea succeeds here).
//   Root cause (confirmed by direct measurement, not assumed from
//   resemblance to case 12): case 07's own Union/A-B WRONG-VOLUME (short by
//   ~0.79 / ~0.75 out of ~22 / ~17) is NOT a classification, welding or
//   assembly bug - direct DINO8_BOOL_DEBUG=1 tracing shows cylinder A's own
//   wall face fragments into EXACTLY the right topology (one Out fragment,
//   correctly carrying 2 holes - the two disjoint lens-shaped patches where
//   the smaller cylinder B's wall pierces through A's - plus the 2 matching
//   interior/In fragments), and Intersection/B-A (which only ever use the 2
//   In fragments and B's own pieces, never that one holed Out fragment)
//   were already exact. The deficiency's own MAGNITUDE is the unmistakable
//   signature of a resolution-dependent tessellation-quality bug, not a
//   fixed topological leak: measured directly, the Union/A-B error HALVES
//   with every doubling of the sweep's own tessellation resolution (0.7931
//   / 0.7532 at 32x128, 0.4200 / 0.3967 at 64x256, 0.2381 / 0.2187 at
//   128x512 - a dropped/duplicated/misclassified fragment would instead
//   show a gap that does NOT shrink with resolution). Root cause: brep.cpp's
//   own ResolveFace() only routes a face to the accurate
//   NurbsSurface::TessellateGridClippedExact() tessellation when that
//   face's own holes list is EMPTY (`exact_clip = !outer.empty() &&
//   holes.empty()`) - a face built with a genuine ON_BrepLoop::inner hole
//   (exactly what SplitFaceLoop() builds for a closed intersection chain
//   entirely interior to a trim, cylinder A's own wall face here) is
//   instead routed to the much cruder NurbsSurface::TessellateGrid()
//   whole-cell fallback, which drops an entire grid cell outright the
//   moment even ONE of its 4 corners tests outside outer-minus-holes - a
//   real, systematic O(cell size) UNDER-estimate. This is the EXACT SAME
//   underlying shared-tessellation defect session 2 above diagnosed (but
//   left unfixed there) for case 12 - confirmed the same mechanism by that
//   identical halving signature, not merely assumed by resemblance, and
//   the two fixtures are otherwise unrelated (a cylinder wall vs. a sphere,
//   an ordinary interior hole vs. one touching the domain's own seam).
//   Fix: BridgeHolesIntoOuter() (this file, right below SplitFaceLoop() and
//   its own siblings), called once per kept fragment right after
//   SplitFaceLoop() assigns it its holes, folds each hole directly into its
//   own fragment's outer loop as a "keyhole" - a thin, GENUINELY non-zero-
//   width corridor from a point near the outer boundary in to the hole and
//   back out - so BuildLoop() only ever emits ONE ON_BrepLoop::outer for
//   the assembled face, no ON_BrepLoop::inner at all, and ResolveFace()
//   picks the accurate exact-clip path automatically, with NO changes to
//   brep.cpp/surface.cpp's shared tessellation pipeline at all (this file's
//   own top comment already discloses why touching that shared, broadly-
//   relied-on code safely was out of reach for one session, in the case-12
//   entry above). This IS the "keyhole-bridging" idea case 12's own session
//   tried and abandoned - but that attempt used a ZERO-width bridge (walk
//   out to a hole vertex and immediately back through the exact SAME
//   point), root-caused THIS session (via BridgeHolesIntoOuter()'s own
//   development, not assumed) as the reason it failed: an exact 180-degree
//   reversal has cross product EXACTLY zero, invisible to BOTH
//   detail::IsSimplePolygon() (which only flags a PROPER transverse
//   crossing, by its own documented design) AND surface.cpp's own
//   IsConvexPolygon() (whose collinear-vertex skip, `abs(turn) < 1e-12`,
//   can't see a zero-turn spike as a convexity violation either) - so the
//   genuinely concave, notched polygon silently misclassifies as convex and
//   gets routed through TessellateGridClippedExact()'s FAST Sutherland-
//   Hodgman ClipConvex() path (which assumes convexity and has no way to
//   represent "and also carve out this notch"), instead of the correct
//   concave ClipPolygon() fallback - a wrong result close to the full outer
//   area, notch silently ignored, not a subtle imprecision (matching case
//   12's own session's own measured "213 sq units on a ~28 sq unit
//   sphere"). Fixed by construction this session: every one of the 4 new
//   bridge vertices sits a small but genuinely NON-ZERO fraction
//   (kEdgeFraction, 0.1%) of one real edge's own length away from the
//   nearest existing outer/hole vertex, giving the notch real, non-
//   collinear corners IsConvexPolygon() correctly flags, at the cost of a
//   geometrically negligible sliver of area and one small, already-
//   disclosed-class T-junction (see this file's own "not yet
//   IsClosedManifold()" paragraph above) per bridged hole - never a new
//   category of defect, and provably harmless to both ON_Brep::IsValid()
//   and volume measurement (see BridgeHolesIntoOuter()'s own doc comment
//   for the full argument). A SECOND, independent bug was caught and fixed
//   during this same development, before it ever shipped: bridging TWO
//   holes on the SAME face by mutating the outer loop sequentially (bridge
//   hole 1 into outer, THEN search for hole 2's own nearest attachment
//   point against that already-mutated outer) let hole 2's search land
//   INSIDE hole 1's own just-inserted, extremely thin notch on a fixture
//   with two close-together holes (sweep case 16, "torus+box half torus in
//   box" - the box's own face has 2 holes from the torus's own two profile
//   circles) - IsSimplePolygon() and the area cross-check both still
//   passed (neither is sensitive to a merely-very-short, not zero-length,
//   edge), yet the result had two numerically-coincident consecutive trim
//   points, a genuine ON_Brep::IsValid() regression on case 16's own Union
//   (caught by this file's own full-sweep before/after diff before it was
//   ever committed, not released). Fixed by having every hole's own
//   candidate search run against the face's ORIGINAL, unmutated outer loop
//   (never a partially-bridged one still growing from an earlier hole in
//   the same call), assembling all accepted bridges into the final outer
//   in one combined pass; `reserved` additionally blocks two holes from
//   ever choosing the same or an adjacent original outer index, as a
//   second, independent guard. A THIRD, more subtle bug, also caught before
//   shipping: a closed intersection chain is stored with its own closing
//   point deliberately repeated (front and back the SAME point - see this
//   file's own "IntersectFaces() already told us this curve is a closed
//   loop" comment below, needed for THIS file's own closed-vs-open test),
//   unlike the "N distinct points, implicit wrap" convention every other
//   loop in this file uses - left alone, walking "every hole point except
//   the one at the bridge" still crossed that repeated point mid-walk,
//   inserting the same physical point twice in a row (the exact same
//   coincident-point failure as the sequential-mutation bug above, but on
//   a SINGLE hole, no second hole involved) - fixed by normalizing a hole
//   back to the no-repeated-point convention once, up front, before
//   BridgeHolesIntoOuter() reasons about its own indices at all.
// VERIFIED: sweep case 07 goes from 2/4 OK (Union/A-B WRONG-VOLUME,
// Intersection/B-A already OK) to 4/4 OK; case 12 goes from 2/4 OK
// (Union/B-A WRONG-VOLUME) to 4/4 OK; a full 76-line before/after verdict
// diff confirms these are the ONLY 4 lines that changed anywhere in the
// sweep - zero regressions on the other 72. New
// TestBooleanCombineGeneralUnequalRadiusPerpendicularCylinders
// (test_basic.cpp) asserts IsValid() and the closed-form 1-D quadrature
// volume for all four op/order combinations plus a tessellation-doubling
// convergence check, mirroring this file's own existing general-boolean
// test template. The full dino8_kernel_tests suite (1561 checks, 9 of them
// new from that test) remains 100% passing.
//
// STILL NOT FIXED, diagnosed as far as an earlier session went:
//   - Sweep case 08 (cyl+cyl SKEW perpendicular axes): improved but not
//     fixed - (m)'s cleanup pass measurably reduces the self-intersecting-
//     trim count on every op (Union 4 to 2, Intersection 3 to 2, A-B 3 to
//     2, B-A unchanged at 3), and Union/Intersection/A-B no longer throw
//     "trim_polygon must be simple" at all, but now fail ON_Brep::IsValid()
//     instead with "is on a closed surface... contains boundary trims ...
//     They should be seam trims connected to the same edge" - a topology-
//     level seam-pairing defect ON_Brep::SetTrimIsoFlags() surfaces, not
//     directly diagnosed this session. B-A still throws the original
//     self-intersecting-trim exception. The true skew (non-intersecting-
//     axis) geometry here is qualitatively different from every other
//     wrap-cut fixture fixed this session (the wrap-cut ellipse's own
//     plane is not just tilted relative to one cylinder's axis, as in case
//     03, but the two cylinders' axes do not meet at all), and is left for
//     a future session's own direct trace.
//   - Sweep case 06 (cyl+cyl perpendicular equal radii, Steinmetz): NOT
//     attempted this session, per this same sweep's own ranked diagnosis -
//     a qualitatively different, harder bug (intersection chains crossing
//     on the SAME face) than the wrap-cut/refinement family (k)-(m) above.
//     Still throws "an edge is claimed by 3 or more fragment loops" on
//     Intersection/A-B/B-A.
#include "dino8/kernel/boolean_general.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dino8/kernel/detail/polygon2d.h"
#include "dino8/kernel/surface_intersect.h"

namespace dino8::kernel {

namespace {

constexpr double kWeldTol = 1e-6;

struct UVPt {
  Point3d p;
  Point2d uv;
};
using Chain = std::vector<UVPt>;

double Dist2(const Point2d& a, const Point2d& b) {
  const double dx = a.x - b.x, dy = a.y - b.y;
  return dx * dx + dy * dy;
}

// `v`'s own fraction along the straight chord p0 -> p1 (unclamped - a
// point slightly outside [0, 1] from ordinary Newton/round-off noise at
// an endpoint is still meaningful, just close to 0 or 1). Shared by
// ReconcileFragmentBoundaries() (below, pre-tessellation fragment loops)
// and ReconcileEdgeTopology() (this file's own tessellation-time pass,
// much further down) - same chord-snap technique, two different stages.
double ProjectT(const Point3d& p0, const Point3d& p1, const Point3d& v) {
  const double abx = p1.x - p0.x, aby = p1.y - p0.y, abz = p1.z - p0.z;
  const double len2 = abx * abx + aby * aby + abz * abz;
  if (len2 < 1e-30) return 0.0;
  const double apx = v.x - p0.x, apy = v.y - p0.y, apz = v.z - p0.z;
  return (apx * abx + apy * aby + apz * abz) / len2;
}

Point3d ChordPoint(const Point3d& p0, const Point3d& p1, double t) {
  return Point3d(p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t, p0.z + (p1.z - p0.z) * t);
}

// --- vertex welding (self-contained copy of brep.cpp's own VertexWelder
// pattern - not exported from that translation unit) ------------------
struct WeldKey {
  long long x = 0, y = 0, z = 0;
  bool operator==(const WeldKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct WeldKeyHash {
  size_t operator()(const WeldKey& k) const {
    size_t h = std::hash<long long>()(k.x);
    h ^= std::hash<long long>()(k.y) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= std::hash<long long>()(k.z) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};
class VertexWelder {
 public:
  int Weld(const Point3d& p) {
    const WeldKey key{std::llround(p.x / kWeldTol), std::llround(p.y / kWeldTol), std::llround(p.z / kWeldTol)};
    const auto it = index_of_.find(key);
    if (it != index_of_.end()) return it->second;
    const int id = static_cast<int>(points_.size());
    points_.push_back(p);
    index_of_.emplace(key, id);
    return id;
  }
  const std::vector<Point3d>& Points() const { return points_; }

 private:
  std::unordered_map<WeldKey, int, WeldKeyHash> index_of_;
  std::vector<Point3d> points_;
};

// --- per-face boundary loop (real trim loop if there is one, else the
// surface's own full parameter domain rectangle for an untrimmed face) --
std::vector<UVPt> FaceBoundaryLoop(const ON_Brep& brep, int face_index, int samples_per_edge = 24) {
  const ON_BrepFace& face = brep.m_F[face_index];
  const ON_Surface* s = face.SurfaceOf();
  std::vector<UVPt> out;
  if (face.m_li.Count() > 0) {
    const ON_BrepLoop& loop = brep.m_L[face.m_li[0]];
    for (int k = 0; k < loop.m_ti.Count(); ++k) {
      const ON_BrepTrim& trim = brep.m_T[loop.m_ti[k]];
      const ON_Curve* c2 = trim.TrimCurveOf();
      if (!c2) continue;
      const ON_Interval d = trim.Domain();
      // c2->IsLinear() only says the trim's own 2D (u, v) path is a
      // straight line - true for every edge of a periodic surface's own
      // full-sweep trim rectangle, including the two that run ALONG the
      // periodic direction (e.g. a full-circle rim at constant height on
      // a cylindrical wall). Those two are NOT straight in 3D at all -
      // their "linear" 2D path sweeps the ENTIRE periodic range, so its
      // 3D image is the surface's own full rim circle. Sampling just 2
      // points for one of these (this function's own fast path for a
      // genuinely straight edge) collapses that whole circle down to a
      // single chord, which welds into a degenerate 2-vertex "digon" edge
      // once a fragment boundary reuses it verbatim (a confirmed defect:
      // ON_Brep::IsValid() rejects the reconstructed box-vs-cylinder
      // Union over exactly this, a stale seam-iso-flag mismatch on that
      // digon's own two half-edges). Guard against it directly: only
      // trust the 2D linearity test when the 3D image really is straight
      // too (checked once, cheaply, via the actual midpoint - a genuinely
      // straight edge's true surface midpoint sits on its own end-to-end
      // chord; a swept periodic direction's does not, by a wide margin).
      bool truly_linear = c2->IsLinear();
      if (truly_linear) {
        const ON_2dPoint uv0 = c2->PointAt(d.Min()), uv1 = c2->PointAt(d.Max()), uvm = c2->PointAt(d.Mid());
        const Point3d p0 = s->PointAt(uv0.x, uv0.y), p1 = s->PointAt(uv1.x, uv1.y), pm = s->PointAt(uvm.x, uvm.y);
        const double chord = p0.DistanceTo(p1);
        const Point3d mid_of_chord = Point3d(0.5 * (p0.x + p1.x), 0.5 * (p0.y + p1.y), 0.5 * (p0.z + p1.z));
        truly_linear = pm.DistanceTo(mid_of_chord) <= std::max(1e-6, 1e-6 * chord);
      }
      const int n = std::max(2, truly_linear ? 2 : samples_per_edge);
      for (int i = 0; i < n; ++i) {
        const ON_2dPoint uv = c2->PointAt(d.ParameterAt(static_cast<double>(i) / n));
        out.push_back({s->PointAt(uv.x, uv.y), uv});
      }
    }
    if (out.size() >= 3) return out;
    out.clear();
  }
  const ON_Interval du = s->Domain(0), dv = s->Domain(1);
  auto add_edge = [&](double u0, double v0, double u1, double v1) {
    for (int i = 0; i < samples_per_edge; ++i) {
      const double t = static_cast<double>(i) / samples_per_edge;
      const double u = u0 + (u1 - u0) * t, v = v0 + (v1 - v0) * t;
      out.push_back({s->PointAt(u, v), Point2d(u, v)});
    }
  };
  add_edge(du.Min(), dv.Min(), du.Max(), dv.Min());
  add_edge(du.Max(), dv.Min(), du.Max(), dv.Max());
  add_edge(du.Max(), dv.Max(), du.Min(), dv.Max());
  add_edge(du.Min(), dv.Max(), du.Min(), dv.Min());
  return out;
}

std::vector<Point2d> ToPoly(const std::vector<UVPt>& loop) {
  std::vector<Point2d> poly;
  poly.reserve(loop.size());
  for (const UVPt& p : loop) poly.push_back(p.uv);
  return poly;
}

// --- splicing one open chain (both ends on the loop's own boundary)
// into a loop, bisecting it in two -------------------------------------
struct BoundaryHit {
  size_t edge_index = 0;
  double t = 0;
};
BoundaryHit NearestOnLoop(const std::vector<UVPt>& loop, const Point2d& q) {
  BoundaryHit best;
  double best_d = 1e300;
  const size_t n = loop.size();
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a = loop[i].uv;
    const Point2d& b = loop[(i + 1) % n].uv;
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 1e-18 ? ((q.x - a.x) * dx + (q.y - a.y) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const Point2d proj(a.x + dx * t, a.y + dy * t);
    const double d = Dist2(proj, q);
    if (d < best_d) {
      best_d = d;
      best = {i, t};
    }
  }
  return best;
}

struct Fragment {
  std::vector<UVPt> outer;
  std::vector<std::vector<UVPt>> holes;
};

// One face can be crossed by several DIFFERENT opposing faces at once, and
// the true intersection curve is only ever complete once those per-pair
// pieces are chained together: where solid A's own face meets both solid
// B's face-1 and face-2 along their common corner, the piece IntersectFaces
// returns for (A, B-face-1) ends at that corner - a point strictly INSIDE
// A's own trim, not on A's own boundary at all - and the (A, B-face-2)
// piece continues from that same 3D point onward. Stitching every
// same-face piece at shared 3D endpoints (within `tol`) before ever asking
// "is this open or closed, and where does it meet the trim boundary" is
// what makes that boundary test meaningful; treating each pair's own piece
// as already complete (this engine's own first, incorrect draft) silently
// leaves most of a real multi-face solid's own boundary unsplit.
Chain ReverseChain(Chain c) {
  std::reverse(c.begin(), c.end());
  return c;
}
// Appends `tail` to `head`, which already ends at (within the stitch
// tolerance) tail's own first point. That shared junction point normally
// carries the same (u, v) in both pieces (each piece refined it
// independently on this same face, to Newton noise) and is kept once. It
// is kept TWICE - both copies, same 3D point - when the two (u, v) differ
// materially: at a degenerate pole (every u is the same 3D point, so the
// two pieces legitimately arrive at the pole at DIFFERENT u values) or
// across a periodic seam (u_max vs u_min). Dropping one copy there draws a
// bogus diagonal in (u, v) between the survivor and the other piece's next
// sample - confirmed on the sphere+box fixture, where the meridian arc's
// own pole end (u = pi/2, v = pi/2) was dropped in favour of the seam
// arc's (u ~ 0.26, v = pi/2), skewing the corner fragment's own (u, v)
// polygon and handing a sliver of the octant to the wrong fragment. With
// both copies kept, the polygon runs along the pole line between them (a
// zero-length 3D edge, collapsed at assembly by CollapseDuplicateVids()).
void AppendStitched(Chain& head, const Chain& tail) {
  constexpr double kSameUV = 1e-7;
  size_t from = 0;
  if (!head.empty() && !tail.empty() && Dist2(head.back().uv, tail.front().uv) <= kSameUV * kSameUV) from = 1;
  head.insert(head.end(), tail.begin() + static_cast<long>(from), tail.end());
}
std::vector<Chain> StitchChains(std::vector<Chain> chains, double tol) {
  const double tol2 = tol * tol;
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < chains.size() && !changed; ++i) {
      if (chains[i].size() < 2) continue;
      const Point3d& ai = chains[i].front().p;
      const Point3d& bi = chains[i].back().p;
      if ((ai - bi).LengthSquared() <= tol2) continue;  // already closed
      for (size_t j = i + 1; j < chains.size(); ++j) {
        if (chains[j].size() < 2) continue;
        const Point3d& aj = chains[j].front().p;
        const Point3d& bj = chains[j].back().p;
        auto is_close = [&](const Point3d& x, const Point3d& y) { return (x - y).LengthSquared() <= tol2; };
        Chain merged;
        bool ok = true;
        if (is_close(bi, aj)) {
          merged = chains[i];
          AppendStitched(merged, chains[j]);
        } else if (is_close(bi, bj)) {
          merged = chains[i];
          AppendStitched(merged, ReverseChain(chains[j]));
        } else if (is_close(ai, aj)) {
          merged = ReverseChain(chains[i]);
          AppendStitched(merged, chains[j]);
        } else if (is_close(ai, bj)) {
          merged = chains[j];
          AppendStitched(merged, chains[i]);
        } else {
          ok = false;
        }
        if (ok) {
          chains[i] = std::move(merged);
          chains.erase(chains.begin() + static_cast<long>(j));
          changed = true;
          break;
        }
      }
    }
  }
  std::vector<Chain> out;
  for (Chain& c : chains)
    if (c.size() >= 2) out.push_back(std::move(c));
  return out;
}

// Splits `loop` into two fragments' outer boundaries at `chain`'s own two
// endpoints (already on, or very near, `loop`'s own boundary - guaranteed
// by IntersectFaces() clipping the curve to the trimmed region).
std::pair<std::vector<UVPt>, std::vector<UVPt>> SpliceOpenChain(const std::vector<UVPt>& loop, const Chain& chain) {
  const BoundaryHit h0 = NearestOnLoop(loop, chain.front().uv);
  const BoundaryHit h1 = NearestOnLoop(loop, chain.back().uv);
  struct Ins {
    size_t edge;
    double t;
    int which;
  };
  std::vector<Ins> ins = {{h0.edge_index, h0.t, 0}, {h1.edge_index, h1.t, 1}};
  std::sort(ins.begin(), ins.end(), [](const Ins& a, const Ins& b) {
    return a.edge < b.edge || (a.edge == b.edge && a.t < b.t);
  });
  std::vector<UVPt> aug;
  int insert_idx[2] = {-1, -1};
  const size_t n = loop.size();
  size_t ii = 0;
  for (size_t e = 0; e < n; ++e) {
    aug.push_back(loop[e]);
    while (ii < ins.size() && ins[ii].edge == e) {
      insert_idx[ins[ii].which] = static_cast<int>(aug.size());
      aug.push_back(ins[ii].which == 0 ? chain.front() : chain.back());
      ++ii;
    }
  }
  const int i0 = insert_idx[0], i1 = insert_idx[1];
  const size_t m = aug.size();
  if (i0 < 0 || i1 < 0 || i0 == i1) {
    // Degenerate (both endpoints landed at the same spot) - refuse to
    // splice rather than fabricate a bogus split.
    return {loop, {}};
  }
  std::vector<UVPt> arcA, arcB;
  for (size_t k = static_cast<size_t>(i0); k != static_cast<size_t>(i1); k = (k + 1) % m) arcA.push_back(aug[k]);
  arcA.push_back(aug[i1]);
  for (size_t k = static_cast<size_t>(i1); k != static_cast<size_t>(i0); k = (k + 1) % m) arcB.push_back(aug[k]);
  arcB.push_back(aug[i0]);

  std::vector<UVPt> fragA = arcA;  // p0 -> boundary -> p1
  for (int idx = static_cast<int>(chain.size()) - 2; idx >= 1; --idx) fragA.push_back(chain[static_cast<size_t>(idx)]);
  std::vector<UVPt> fragB = arcB;  // p1 -> boundary -> p0
  for (size_t idx = 1; idx + 1 < chain.size(); ++idx) fragB.push_back(chain[idx]);
  return {fragA, fragB};
}

// A 3D-closed chain (front and back coincide) that is really a single FULL
// SWEEP of one of `s`'s own periodic (u or v) directions - e.g. the circle
// where a plane exactly perpendicular to a cylinder's axis cuts its
// periodic wall - is NOT an island to hole out inside the face's trim: in
// flat (u, v) terms it runs from one side of that direction's domain to
// the other (every u value is visited exactly once), which is exactly what
// FaceBoundaryLoop() already represents as the face's own two SEAM edges
// (see that function's own doc comment: a full-sweep periodic face's trim
// quad has both seam sides as distinct edges, both being the same real 3D
// edge). Confirmed root cause of the box-pierced-by-a-perpendicular-
// cylinder case: treating this loop as a hole put both the "above the cut"
// and "below the cut" bands of the wall into ONE fragment (the whole
// boundary, holed out by the two circles) while ALSO spinning each circle
// off as its own separate degenerate interior fragment - self-intersecting
// nonsense once flattened, and outright wrong topology (the two bands are
// obviously not one connected region of the wall). Detected by walking the
// closed chain's own consecutive (u, v) samples (wrapping once, since the
// chain repeats its own first point per this file's own closed-chain
// convention) for a jump exceeding half a periodic direction's domain
// length - the same signal surface_intersect.cpp's SplitAtSeams() already
// uses to find a seam crossing. Exactly one such crossing means "single
// full sweep, cut it open right there"; zero means a genuine island
// (returns false, left as a closed chain unchanged); more than one is a
// more exotic case (e.g. wrapping the periodic direction twice) this
// engine does not attempt to untangle, and is also left as-is (closed,
// which will fail loudly rather than silently corrupt if it can't be
// fragmented sanely).
bool SplitPeriodicWrapChain(const Chain& c, const ON_Surface& s, Chain& out_open) {
  if (c.size() < 3) return false;
  // Drop a literal trailing duplicate of the front point (this file's own
  // convention for a chain built from an already-closed IntersectionCurve)
  // so seam-jump detection below walks only the curve's own distinct
  // samples, with the implicit wrap edge (last -> first) checked exactly
  // once via the modulo index below.
  Chain pts = c;
  if (pts.size() >= 2 && (pts.front().p - pts.back().p).Length() <= kWeldTol * 100) pts.pop_back();
  const size_t n = pts.size();
  if (n < 3) return false;
  int crossings = 0;
  size_t cut_at = 0;
  int wrap_dir = -1;
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    for (int dir = 0; dir < 2; ++dir) {
      if (!s.IsClosed(dir)) continue;
      const double L = s.Domain(dir).Length();
      if (L <= 0) continue;
      const double vi = dir == 0 ? pts[i].uv.x : pts[i].uv.y;
      const double vj = dir == 0 ? pts[j].uv.x : pts[j].uv.y;
      if (std::fabs(vi - vj) > 0.5 * L) { ++crossings; cut_at = i; wrap_dir = dir; }
    }
  }
  if (crossings != 1) return false;
  out_open.clear();
  out_open.reserve(n);
  for (size_t k = 0; k < n; ++k) out_open.push_back(pts[(cut_at + 1 + k) % n]);
  // The two new "ends" (out_open.front()/back(), the pair that straddled
  // the seam) are ordinary curve samples near the seam, not points
  // Newton-refined to sit exactly on it - typically off by up to one mesh
  // cell's worth of parameter (see DivisionsFor() in surface_intersect.cpp),
  // which is nowhere near SplitFaceLoop()'s own splice tolerance (it
  // requires an open chain's endpoint to already sit almost exactly on the
  // fragment boundary it's meant to splice into, since every OTHER open
  // chain's endpoints really are that precise, being clipped there by
  // IntersectFaces() itself).
  //
  // These two points are ALSO shared, by 3D coincidence, with this SAME
  // circle's corresponding chain on the OTHER (non-periodic) face of this
  // pair - e.g. the box's own flat face, which sees this circle as an
  // ordinary interior island, not a wrap-cut, and so never touches or
  // moves its own copies of these points at all. Overwriting one side's
  // (u, v) to sit exactly on the seam WITHOUT moving its own 3D point `p`
  // to match keeps cross-face welding correct (both sides still agree on
  // that shared 3D point) but leaves THIS face's own reconstructed
  // edge/trim pair self-inconsistent - its trim curve's endpoint (u, v)
  // no longer evaluates back to its edge curve's own endpoint 3D location
  // (off by a full mesh cell's worth of arc length), which is exactly
  // what later fails ON_Brep::IsValid()'s own trim-vs-edge distance check.
  // Confirmed by direct before/after IsValid() text-log tracing on the
  // box-vs-perpendicular-cylinder fixture.
  //
  // Fixed by NOT moving the shared point at all: instead, splice in one
  // brand-new vertex right at each end, genuinely ON the seam (exact
  // (u, v) AND a freshly surface-evaluated `p` that matches it exactly,
  // self-consistent for this face's own trim/edge pair) while leaving the
  // original near-seam sample in place, one step further into the chain,
  // still carrying its original, cross-face-shared (u, v)/`p` pair
  // unchanged. The tiny extra segment this adds (one mesh cell's worth of
  // arc, wholly within this one face, welded to nothing on the other
  // side) is a real, if small, extra facet of this face alone - not an
  // approximation error, since both its own endpoints are exact for THIS
  // face's own surface.
  //
  // FORMERLY-REMAINING BUG, NOW FIXED: the two new vertices (front_seam at
  // one end of the periodic direction, back_seam at the other) are BOTH
  // meant to sit at the exact same location along the surface's OTHER
  // ("cross") direction - e.g. the same axial height on a cylinder wall,
  // the same latitude on a sphere - since a periodic surface's u == lo and
  // u == hi sides are the literal same 3D curve, and this one wrap-cut
  // crosses that curve at exactly ONE physical point (this is not two
  // different points, it is the SAME point expressed on both sides of the
  // domain rectangle). The code used to take that cross-direction value
  // from each end's own nearby ORIGINAL sample (out_open.front()/back())
  // independently - two different, already-refined points on the real
  // curve, each carrying its own small, independent Newton-residual noise
  // in that direction. For a curve tangent to the wrap direction (a level
  // circle, the common case), that noise is normally far below drawing
  // tolerance, but confirmed by direct tracing on sphere+cylinder (sweep
  // case 13, a cylinder wall pierced by a sphere along its own axis): the
  // two independent values differed by ~1e-6, just ABOVE this file's own
  // kWeldTol, so front_seam and back_seam's shared physical point produced
  // TWO vertices that failed to weld into one - a genuine "Line points are
  // coincident" / zero-length-2D-trim ON_Brep::IsValid() failure (this
  // one is a 2D degeneracy: BuildLoop() draws a trim between two
  // *different* vertices that nonetheless carry the identical (u, v),
  // since the seam-side loop sample this point later merges with was
  // snapped from the SAME nearby original sample). Root-caused and fixed:
  // compute ONE shared cross-direction value from the two samples that
  // actually straddle the seam crossing (out_open.back() -> out_open.front(),
  // the very pair the crossing scan above found), linearly interpolated at
  // the exact point their wrap-direction values cross the seam - the same
  // interpolate-then-pin idea surface_intersect.cpp's own SeamCrossing()
  // uses for a genuine surface-pair seam crossing, specialized here to a
  // single already-known polyline (no second surface / Newton solve
  // needed: the two straddling samples already bracket the true crossing
  // tightly enough that a linear blend is accurate to well within
  // kWeldTol) - and use that ONE value for BOTH new vertices, so they are
  // computed from IDENTICAL inputs rather than two merely-close ones. This
  // also directly helps a SLOPED or SKEW wrap-cut (the cutting surface is
  // not exactly perpendicular to the periodic axis, so the two straddling
  // samples' cross-direction values differ by more than mere Newton noise
  // - interpolating between them is then not just noise-cancelling but the
  // geometrically correct thing to do, unlike either endpoint's own value
  // alone).
  //
  // A FURTHER, SEPARATE degeneracy, also root-caused on this same sweep
  // case 13 fixture: the two samples that straddle the crossing
  // (out_open.back()/front(), found by the scan above) are not always an
  // ORDINARY pair merely near the seam - for a curve whose own upstream
  // discretization happens to seed a sample exactly AT one domain end (a
  // Newton solve's own hard clamp to the domain, see NewtonSolve()'s use
  // of Clamp() in surface_intersect.cpp, lands bit-exactly on `lo`/`hi`
  // when the unclamped solution would overshoot the domain), BOTH
  // straddling samples can already sit bit-exactly ON the seam (one at
  // `lo`, one at `hi`) before this function does anything at all. In that
  // case the OLD/general code below (insert a brand-new vertex right next
  // to each original sample) put a freshly-surface-evaluated point
  // literally adjacent, in the chain, to an original sample that ALREADY
  // has the exact same wrap-direction value - and, because the new
  // point's cross-direction value was taken from (or, after the fix just
  // above, blended from) that same nearby original sample, the two ended
  // up with IDENTICAL (u, v) but slightly different `p` (the original
  // sample's own small, inherent SSX residual, ~ opt.tolerance, vs. the
  // new point's exact analytic surface evaluation) - a genuine "Line
  // points are coincident" / zero-length-2D-trim ON_Brep::IsValid()
  // failure, confirmed by direct tracing (the residual measured exactly
  // the ~1e-6 the sweep's own diagnosis flagged, landing just above
  // kWeldTol). Fixed the same way CutChainAtDomainBoundary() already
  // handles an on-boundary run elsewhere in this file: when a straddling
  // sample is ALREADY at the seam to within Newton's own convergence (not
  // just "somewhere on the same side" - a tight, absolute check, since
  // this is specifically catching an exact domain-clamp, not the general
  // "up to one mesh cell" case the brand-new-vertex path below still
  // handles), reuse that EXISTING sample as the seam-side end directly -
  // only tidying its own wrap-direction value to the exact domain bound
  // and its cross-direction value to the shared, interpolated
  // `cross_val` - rather than inserting a redundant second vertex
  // negligibly close to it. Its `p` is deliberately left untouched (same
  // reasoning as CutChainAtDomainBoundary's own doc comment): it is still
  // the exact point the OTHER face's own copy of this chain welds
  // against, and the small (u, v)-vs-`p` residual this leaves is the same
  // order as any ordinary open-chain splice endpoint's own SSX tolerance,
  // which ON_Brep::IsValid()'s trim-vs-edge check already tolerates.
  const double lo = s.Domain(wrap_dir).Min(), hi = s.Domain(wrap_dir).Max();
  const double L = hi - lo;
  const UVPt& straddle_before = out_open.back();   // pts[cut_at]
  const UVPt& straddle_after = out_open.front();   // pts[cut_at + 1]
  const double w_before = wrap_dir == 0 ? straddle_before.uv.x : straddle_before.uv.y;
  const double w_after = wrap_dir == 0 ? straddle_after.uv.x : straddle_after.uv.y;
  const double c_before = wrap_dir == 0 ? straddle_before.uv.y : straddle_before.uv.x;
  const double c_after = wrap_dir == 0 ? straddle_after.uv.y : straddle_after.uv.x;
  const double w_after_un = w_after + (w_before > w_after ? L : -L);
  const double seam_before = w_before > w_after ? hi : lo;
  const double denom = w_after_un - w_before;
  const double t = std::fabs(denom) > 1e-300 ? std::clamp((seam_before - w_before) / denom, 0.0, 1.0) : 0.5;
  const double cross_val = c_before + (c_after - c_before) * t;
  const double fu = w_after;
  const bool front_is_lo = std::fabs(fu - lo) < std::fabs(fu - hi);
  // Exact-domain-clamp check: much tighter than "near the seam" - this
  // only ever fires for a sample Newton already snapped bit-exactly to
  // the boundary, never for the ordinary "up to one mesh cell off" case.
  const double exact_eps = 1e-9 * std::max(L, 1.0);
  auto already_at_seam = [&](double w, double snapped) { return std::fabs(w - snapped) <= exact_eps; };
  // `near_pt`, not `near`: the latter is a legacy Windows SDK macro (see
  // the is_close rename elsewhere in this file for the same MSVC break).
  auto make_seam_point = [&](const UVPt& near_pt, double snapped) {
    UVPt sp = near_pt;
    if (wrap_dir == 0) { sp.uv.x = snapped; sp.uv.y = cross_val; } else { sp.uv.y = snapped; sp.uv.x = cross_val; }
    sp.p = s.PointAt(sp.uv.x, sp.uv.y);
    return sp;
  };
  const double front_snap = front_is_lo ? lo : hi;
  const double back_snap = front_is_lo ? hi : lo;
  if (already_at_seam(w_after, front_snap)) {
    // Reuse the existing straddling sample in place: no new vertex.
    if (wrap_dir == 0) { out_open.front().uv.x = front_snap; out_open.front().uv.y = cross_val; }
    else { out_open.front().uv.y = front_snap; out_open.front().uv.x = cross_val; }
  } else {
    out_open.insert(out_open.begin(), make_seam_point(out_open.front(), front_snap));
  }
  if (already_at_seam(w_before, back_snap)) {
    if (wrap_dir == 0) { out_open.back().uv.x = back_snap; out_open.back().uv.y = cross_val; }
    else { out_open.back().uv.y = back_snap; out_open.back().uv.x = cross_val; }
  } else {
    out_open.push_back(make_seam_point(out_open.back(), back_snap));
  }
  return true;
}

// --- chains that run ALONG an untrimmed face's own domain boundary ------
//
// An untrimmed periodic/singular face (a full sphere from Brep::Sphere(),
// the canonical case) has a (u, v) domain rectangle whose sides are not
// real 3D boundaries at all: the two seam sides (u == u_min and u == u_max)
// are the SAME meridian, and a singular side (v == v_max, say) is a single
// 3D point, the pole. An intersection chain can run right along one of
// those sides - a plane through the sphere's centre containing the seam
// meridian produces exactly that, and the fixture in scratch_test.cpp /
// TestBooleanCombineGeneralSphereBox (a box with one corner at the sphere's
// own centre, its three faces on the coordinate planes) does it for BOTH
// the seam AND the north pole at once: the three great-circle arcs stitch
// into one 3D-closed chain that, in (u, v), is the corner square
// [0, pi/2] x [0, pi/2] of the sphere's own domain rectangle, touching the
// rectangle's own left (seam) and top (pole) sides along their full length.
//
// SplitFaceLoop() below would treat that 3D-closed chain as an interior
// island (a hole in the untouched fragment plus its own interior fragment)
// - confirmed wrong: the "hole" then overlapped the outer loop's own seam
// edge, so ON_Brep::IsValid() failed for Union/Difference and their
// volumes were measurably short. The right split is the ordinary
// open-chain one: bisect the rectangle at the two points where the chain
// leaves/re-enters its boundary, giving the corner square and the notched
// rest.
//
// CutChainAtDomainBoundary() does exactly that, in three steps, and only
// for an untrimmed face with a genuinely seam/singular side:
//   1. classify every chain point as ON one such side (3D distance from
//      the point to the surface evaluated at its (u, v) snapped onto that
//      side is within `tol` - a pole side snaps to the pole itself, so it
//      takes priority over a seam side, since at the pole every u is the
//      seam) and snap those points' (u, v) onto the side exactly (their
//      shared 3D point `p` is deliberately NOT moved: it is the same point
//      the other face's own copy of this chain welds against, and
//      IsValid()'s own trim-vs-edge end check tolerates far more than the
//      SSX-tolerance-sized residual this leaves on this face alone);
//   2. splice each maximal run (>= 2 consecutive on-boundary points; an
//      isolated near-seam sample of a chain crossing the seam
//      transversally is left entirely alone, so the wrap-cut path stays
//      untouched) INTO the face's own boundary loop, REPLACING the loop's
//      own samples over that run's span on that side - and on the seam's
//      twin side too, with the same 3D points, so the two seam sides stay
//      bit-identical (they weld into ONE seam edge, as they must) and the
//      run's own points, being the other face's exact copies, make the
//      shared cut a literal shared ON_BrepEdge rather than two
//      differently-sampled polylines of the same arc;
//   3. cut the chain at those runs: every stretch of off-boundary points,
//      extended by the bounding run point at each end, becomes an ordinary
//      open chain whose two ends are now exact loop vertices, spliced by
//      SplitFaceLoop() like any other open chain.
// Returns false (chain untouched, loop untouched) when no run exists.
bool CutChainAtDomainBoundary(const Chain& c, const ON_Surface& s, double tol, std::vector<UVPt>& loop,
                              std::vector<Chain>& out_open) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  // Side numbering follows ON_Surface::IsSingular(): 0 = south (v_min),
  // 1 = east (u_max), 2 = north (v_max), 3 = west (u_min).
  bool singular[4], seam[4];
  bool any = false;
  for (int side = 0; side < 4; ++side) {
    singular[side] = s.IsSingular(side);
    const int dir = (side == 1 || side == 3) ? 0 : 1;
    seam[side] = !singular[side] && s.IsClosed(dir);
    any = any || singular[side] || seam[side];
  }
  if (!any || c.size() < 2) return false;

  Chain pts = c;
  const bool closed = pts.size() >= 3 && (pts.front().p - pts.back().p).Length() <= tol;
  // A closed chain's own repeated closing point is dropped only when it
  // really is a repeat in (u, v) too - StitchChains() deliberately keeps
  // both copies of a pole/seam junction (same 3D point, different (u, v);
  // see AppendStitched()), and they can land exactly at a chain's own two
  // ends, where both are needed to keep the (u, v) polygon continuous.
  if (closed && Dist2(pts.front().uv, pts.back().uv) <= 1e-14) pts.pop_back();
  const size_t n = pts.size();
  if (n < 2) return false;

  auto snapped_uv = [&](const Point2d& uv, int side) {
    switch (side) {
      case 0: return Point2d(uv.x, dv.Min());
      case 1: return Point2d(du.Max(), uv.y);
      case 2: return Point2d(uv.x, dv.Max());
      default: return Point2d(du.Min(), uv.y);
    }
  };
  auto on_side = [&](const UVPt& q, int side) {
    if (!singular[side] && !seam[side]) return false;
    if (seam[side]) {
      // Only claim the seam side this point is actually nearer to in
      // (u, v), so its snapped copy stays continuous with its neighbours.
      const int dir = (side == 1 || side == 3) ? 0 : 1;
      const ON_Interval d = s.Domain(dir);
      const double val = dir == 0 ? q.uv.x : q.uv.y;
      const bool nearer_min = (val - d.Min()) < (d.Max() - val);
      if ((side == 1 || side == 2) == nearer_min) return false;
    }
    const Point2d suv = snapped_uv(q.uv, side);
    return (s.PointAt(suv.x, suv.y) - q.p).Length() <= tol;
  };
  std::vector<int> side_of(n, -1);
  for (size_t k = 0; k < n; ++k) {
    for (int side : {0, 2, 1, 3}) {  // singular (pole) sides first
      if (on_side(pts[k], side)) { side_of[k] = side; break; }
    }
  }
  // Runs of >= 2 consecutive on-boundary points (cyclic for a closed chain).
  std::vector<char> in_run(n, 0);
  for (size_t k = 0; k < n; ++k) {
    if (side_of[k] < 0) continue;
    const size_t prev = (k + n - 1) % n, next = (k + 1) % n;
    const bool prev_on = (closed || k > 0) && side_of[prev] >= 0;
    const bool next_on = (closed || k + 1 < n) && side_of[next] >= 0;
    if (prev_on || next_on) in_run[k] = 1;
  }
  size_t run_count = 0;
  for (char f : in_run) run_count += f ? 1 : 0;
  if (run_count == 0) return false;
  for (size_t k = 0; k < n; ++k)
    if (in_run[k]) pts[k].uv = snapped_uv(pts[k].uv, side_of[k]);

  // Step 2: splice every per-side sub-run into the loop, replacing the
  // loop's own samples over its span (and on the seam twin side).
  const double eps_u = 1e-9 * std::max(du.Length(), 1.0), eps_v = 1e-9 * std::max(dv.Length(), 1.0);
  auto loop_pt_on_side = [&](const UVPt& q, int side) {
    switch (side) {
      case 0: return std::fabs(q.uv.y - dv.Min()) <= eps_v;
      case 1: return std::fabs(q.uv.x - du.Max()) <= eps_u;
      case 2: return std::fabs(q.uv.y - dv.Max()) <= eps_v;
      default: return std::fabs(q.uv.x - du.Min()) <= eps_u;
    }
  };
  auto along = [&](const Point2d& uv, int side) { return (side == 0 || side == 2) ? uv.x : uv.y; };
  auto twin_of = [&](int side) { return seam[side] ? (side + 2) % 4 : -1; };
  auto splice_subrun = [&](const std::vector<UVPt>& run, int side) {
    if (run.empty()) return;
    double lo = along(run.front().uv, side), hi = lo;
    for (const UVPt& q : run) { lo = std::min(lo, along(q.uv, side)); hi = std::max(hi, along(q.uv, side)); }
    const double eps = (side == 0 || side == 2) ? eps_u : eps_v;
    for (int which = 0; which < 2; ++which) {
      const int sd = which == 0 ? side : twin_of(side);
      if (sd < 0) continue;
      std::vector<UVPt> kept;
      kept.reserve(loop.size() + run.size());
      for (const UVPt& q : loop) {
        if (loop_pt_on_side(q, sd) && along(q.uv, sd) >= lo - eps && along(q.uv, sd) <= hi + eps) continue;
        kept.push_back(q);
      }
      loop.swap(kept);
      struct Ins { size_t edge; double t; UVPt pt; };
      std::vector<Ins> ins;
      for (const UVPt& q : run) {
        UVPt tq = q;
        tq.uv = snapped_uv(q.uv, sd);
        const BoundaryHit h = NearestOnLoop(loop, tq.uv);
        ins.push_back({h.edge_index, h.t, tq});
      }
      std::sort(ins.begin(), ins.end(), [](const Ins& a, const Ins& b) { return a.edge < b.edge || (a.edge == b.edge && a.t < b.t); });
      std::vector<UVPt> aug;
      aug.reserve(loop.size() + ins.size());
      size_t ii = 0;
      for (size_t e = 0; e < loop.size(); ++e) {
        aug.push_back(loop[e]);
        while (ii < ins.size() && ins[ii].edge == e) aug.push_back(ins[ii++].pt);
      }
      loop.swap(aug);
    }
  };
  // Walk the runs (cyclically for a closed chain) starting from a
  // non-run point so no run is split by the array's own wraparound.
  size_t start = 0;
  if (closed) {
    while (start < n && in_run[start]) ++start;
    if (start == n) start = 0;  // entirely on the boundary
  }
  {
    std::vector<UVPt> sub;
    int sub_side = -1;
    for (size_t k = 0; k < n; ++k) {
      const size_t i = (start + k) % n;
      if (in_run[i] && side_of[i] == sub_side) { sub.push_back(pts[i]); continue; }
      splice_subrun(sub, sub_side);
      sub.clear();
      sub_side = -1;
      if (in_run[i]) { sub.push_back(pts[i]); sub_side = side_of[i]; }
    }
    splice_subrun(sub, sub_side);
  }

  // Step 3: the off-boundary stretches, each bounded by a run point. A
  // closed chain is walked cyclically from a run point (revisiting it at
  // the end) so every stretch is bounded on both sides; an open chain is
  // walked from its own first point, so a leading/trailing stretch keeps
  // its ordinary open end (already on the trim, like any open chain).
  out_open.clear();
  if (run_count == n) return true;  // lies entirely on the boundary: nothing left to splice
  size_t walk_start = 0;
  if (closed) {
    while (walk_start < n && !in_run[walk_start]) ++walk_start;
  }
  Chain cur;
  bool cur_has_off = false;
  auto flush = [&]() {
    if (cur_has_off && cur.size() >= 2) out_open.push_back(cur);
    cur.clear();
    cur_has_off = false;
  };
  const size_t total = closed ? n + 1 : n;
  for (size_t k = 0; k < total; ++k) {
    const size_t i = (walk_start + k) % n;
    if (in_run[i]) {
      if (cur_has_off) { cur.push_back(pts[i]); flush(); }
      cur.clear();
      cur.push_back(pts[i]);  // a run point also begins the next stretch
      continue;
    }
    cur.push_back(pts[i]);
    cur_has_off = true;
  }
  if (!closed) flush();
  return true;
}

// Splits one face's own boundary loop into fragments using every
// intersection chain gathered for it. Closed chains become holes (in
// whichever open-chain fragment geometrically contains them) plus their
// own interior fragment; open chains bisect the boundary, applied one at
// a time against whichever current fragment contains both its endpoints.
std::vector<Fragment> SplitFaceLoop(const std::vector<UVPt>& boundary, const std::vector<Chain>& closed_chains,
                                     const std::vector<Chain>& open_chains) {
  std::vector<std::vector<UVPt>> outers = {boundary};
  for (const Chain& c : open_chains) {
    if (c.size() < 2) continue;
    bool applied = false;
    for (size_t i = 0; i < outers.size(); ++i) {
      const BoundaryHit h0 = NearestOnLoop(outers[i], c.front().uv);
      const BoundaryHit h1 = NearestOnLoop(outers[i], c.back().uv);
      const Point2d& e0a = outers[i][h0.edge_index].uv;
      const Point2d& e0b = outers[i][(h0.edge_index + 1) % outers[i].size()].uv;
      const Point2d proj0(e0a.x + (e0b.x - e0a.x) * h0.t, e0a.y + (e0b.y - e0a.y) * h0.t);
      const Point2d& e1a = outers[i][h1.edge_index].uv;
      const Point2d& e1b = outers[i][(h1.edge_index + 1) % outers[i].size()].uv;
      const Point2d proj1(e1a.x + (e1b.x - e1a.x) * h1.t, e1a.y + (e1b.y - e1a.y) * h1.t);
      const double bbox_span = 1.0;  // scale-agnostic relative check below
      (void)bbox_span;
      // Both endpoints must actually sit close to this fragment's own
      // boundary (not just closest-of-the-worklist) for it to be the
      // right one to splice.
      if (std::sqrt(Dist2(proj0, c.front().uv)) > 1e-3 * (1.0 + std::sqrt(Dist2(e0a, e0b))) &&
          std::sqrt(Dist2(proj0, c.front().uv)) > 1e-6) {
        continue;
      }
      auto [fa, fb] = SpliceOpenChain(outers[i], c);
      if (fb.empty()) continue;  // splice refused (degenerate)
      outers[i] = fa;
      outers.push_back(fb);
      applied = true;
      break;
    }
    if (!applied) {
      // Could not find a fragment whose boundary the chain actually
      // touches - drop it rather than corrupt the topology. Disclosed
      // limitation: this can happen for chains that graze a fragment
      // seam produced by an earlier splice.
      continue;
    }
  }

  std::vector<Fragment> frags;
  frags.reserve(outers.size());
  for (std::vector<UVPt>& o : outers) frags.push_back(Fragment{std::move(o), {}});

  for (const Chain& c : closed_chains) {
    if (c.size() < 3) continue;
    const std::vector<Point2d> chain_poly = ToPoly(c);
    int owner = -1;
    for (size_t i = 0; i < frags.size(); ++i) {
      if (PointInPolygon(ToPoly(frags[i].outer), c.front().uv)) {
        owner = static_cast<int>(i);
        break;
      }
    }
    if (owner >= 0) frags[static_cast<size_t>(owner)].holes.push_back(c);
    Fragment interior;
    interior.outer = c;
    frags.push_back(std::move(interior));
  }
  return frags;
}

// One representative (u, v) point strictly inside `outer` and outside
// every hole - tries the centroid first, then a handful of interior
// candidates derived from the loop's own points, for the non-convex case.
bool RepresentativeUV(const Fragment& frag, Point2d& out_uv) {
  const std::vector<Point2d> outer_poly = ToPoly(frag.outer);
  std::vector<std::vector<Point2d>> hole_polys;
  for (const auto& h : frag.holes) hole_polys.push_back(ToPoly(h));
  auto ok = [&](const Point2d& q) {
    if (!PointInPolygon(outer_poly, q)) return false;
    for (const auto& hp : hole_polys)
      if (PointInPolygon(hp, q)) return false;
    return true;
  };
  double cx = 0, cy = 0;
  for (const Point2d& p : outer_poly) {
    cx += p.x;
    cy += p.y;
  }
  cx /= static_cast<double>(outer_poly.size());
  cy /= static_cast<double>(outer_poly.size());
  if (ok(Point2d(cx, cy))) {
    out_uv = Point2d(cx, cy);
    return true;
  }
  // Fallback: average of every consecutive point triple's midpoint,
  // nudged toward the polygon centroid - cheap and adequate for the
  // simple (near-convex) fragments this engine's validated scope covers.
  const size_t n = outer_poly.size();
  for (size_t i = 0; i < n; ++i) {
    const Point2d mid((outer_poly[i].x + cx) * 0.5, (outer_poly[i].y + cy) * 0.5);
    if (ok(mid)) {
      out_uv = mid;
      return true;
    }
  }
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      const Point2d mid((outer_poly[i].x + outer_poly[j].x) / 2.0, (outer_poly[i].y + outer_poly[j].y) / 2.0);
      if (ok(mid)) {
        out_uv = mid;
        return true;
      }
    }
  }
  return false;
}

// --- keyhole bridging: fold a genuinely interior closed hole loop into
// its own fragment's outer loop, so the assembled face ends up hole-free
// -------------------------------------------------------------------
//
// WHY: brep.cpp's own ResolveFace() only routes a face to the accurate
// NurbsSurface::TessellateGridClippedExact() tessellation when that
// face's own holes list is EMPTY (`exact_clip = !outer.empty() &&
// holes.empty()`) - a face built with a genuine ON_BrepLoop::inner hole
// (exactly what SplitFaceLoop() above builds for a closed intersection
// chain entirely interior to a trim) is instead routed to the much
// cruder NurbsSurface::TessellateGrid() whole-cell fallback, which drops
// an entire grid cell outright the moment even ONE of its 4 corners
// tests outside outer-minus-holes - a real, systematic O(cell size)
// UNDER-estimate of the kept region's own area/volume.
//
// ROOT-CAUSED (this session, sweep case 07 "cyl+cyl perpendicular
// unequal radii": Union/A-B WRONG-VOLUME, Intersection/B-A already OK):
// direct DINO8_BOOL_DEBUG=1 tracing shows cylinder A's own wall face
// fragments into EXACTLY the right topology - one Out fragment (the wall
// band exterior to B) correctly carrying 2 holes (the two disjoint
// lens-shaped patches where the smaller cylinder B's wall pierces
// through), plus the 2 matching interior (In) fragments - so this is NOT
// a dropped/duplicated/misclassified fragment (the Out fragment's own
// hole count and the 2 In fragments are all exactly right, and
// Intersection/B-A, which use the SAME 2 In fragments plus B's own
// pieces and never touch this one holed Out fragment, already measure
// correctly). The deficiency is entirely explained by, and its magnitude
// is the unmistakable signature of, this whole-cell tessellation
// crudeness: measured directly, the Union/A-B error (0.7931 / 0.7532 out
// of ~22 / ~17 at the sweep's own 32x128 resolution) HALVES with every
// doubling of that resolution (0.4200 / 0.3967 at 64x256, 0.2381 /
// 0.2187 at 128x512) - a resolution-INDEPENDENT bug (a dropped fragment,
// wrong classification, wrong winding, ...) would show a gap that does
// NOT shrink like this. This is the SAME underlying shared-tessellation
// defect this file's own top comment already diagnosed (but left
// unfixed, as genuinely harder to fix safely) for sweep case 12
// (sphere+sphere) - confirmed the same mechanism here by that identical
// halving signature, not merely assumed by resemblance.
//
// THE FIX: rather than touching the shared, broadly-relied-on
// brep.cpp/surface.cpp tessellation pipeline (case 12's own abandoned
// attempt - see above - correctly identified this as the general correct
// fix but too large a blast radius to verify safely in one session),
// this function avoids the crude path entirely, staying local to this
// file: it merges a hole directly into its own fragment's outer loop as
// a "keyhole" - a thin corridor from a point near the outer boundary in
// to the hole and back out - so BuildLoop() below only ever emits ONE
// ON_BrepLoop::outer for the assembled face, no ON_BrepLoop::inner at
// all, and ResolveFace() picks the accurate exact-clip path automatically
// with NO changes to brep.cpp/surface.cpp whatsoever.
//
// A zero-width version of this same idea (walk out to a hole vertex and
// immediately back through the exact SAME point) was tried in an earlier
// session for case 12 and abandoned there as unsafe: it passes
// detail::IsSimplePolygon() (which only flags a PROPER transverse
// crossing - two edges that merely touch at a shared endpoint, including
// an exact in-and-back-out spike, are invisible to it, by that
// function's own documented design) but still tessellated wildly wrong
// downstream. Root-caused here: an exact 180-degree reversal has cross
// product EXACTLY zero, which surface.cpp's own IsConvexPolygon() (its
// collinear-vertex skip, `abs(turn) < 1e-12`) also can't see as a
// convexity violation - so a genuinely concave, notched polygon built
// with a zero-width spike gets silently misclassified as convex and
// routed through TessellateGridClippedExact()'s FAST Sutherland-Hodgman
// ClipConvex() path (which assumes convexity and has no way to represent
// "and also carve out this notch"), rather than the correct concave
// ClipPolygon() fallback - explaining a result close to the full outer
// area, notch ignored, not a subtle imprecision.
//
// Fixed by construction: every one of the 4 new bridge vertices sits a
// small but genuinely NON-ZERO fraction of one real edge's own length
// away from the nearest existing outer/hole vertex (never reusing an
// exact point twice, never landing exactly at parameter 0 or 1 of an
// existing edge), so the notch has real, non-collinear corners
// IsConvexPolygon() correctly flags as a convexity violation, sending it
// through the (already correct, unmodified) concave path. The nearest
// existing outer and hole vertices themselves are simply skipped
// (replaced by their own two straddling bridge points), losing a
// geometrically negligible sliver of area (see kEdgeFraction below) -
// this only ever shortens the ONE outer edge and ONE hole edge the
// bridge attaches to into two smaller pieces apiece, exactly the same
// kind of independently-tessellated-adjacent-face mismatch this file's
// own top comment already discloses as a known, accepted limitation (the
// "not yet IsClosedManifold()" gap) - not a new category of defect, and
// provably harmless to both ON_Brep::IsValid() (every affected edge still
// gets a consistent, correctly-oriented ON_BrepEdge/ON_BrepTrim pair;
// IsValid() has never required matching subdivision density across a
// shared edge - only this engine's own separate, not-yet-attempted
// IsClosedManifold() check does) and to volume measurement (the
// divergence-theorem integral a tessellated closed mesh computes doesn't
// depend on how finely two adjacent, independently-tessellated faces
// subdivide their own shared boundary, only that each face's own
// triangulation correctly tiles its own trimmed region - which an
// ordinary exact-clip tessellation of a genuinely simple polygon already
// guarantees).
//
// Purely additive and self-verifying, never a regression risk: several
// candidate attachment points are tried per hole, nearest pair first, and
// each candidate is checked directly - detail::IsSimplePolygon() on the
// FULL merged polygon (this hole's own bridge plus every other hole
// already accepted), plus its own signed area matching
// outer_area + sum(hole areas) within generous slack - before being
// accepted; a hole with no checked-out candidate is left completely
// untouched in `holes` for the caller to fall back to the original,
// already-correct-if-cruder separate ON_BrepLoop::inner representation.
//
// Every hole's own candidate search runs against the face's ORIGINAL,
// unmutated outer loop - never against a partially-bridged one still
// growing from an earlier hole in the same call. An earlier version of
// this function bridged holes one at a time, mutating `outer` in place
// between them; on a fixture with two holes close enough together that
// the SECOND hole's nearest attachment point ended up landing inside the
// FIRST hole's own just-inserted, already extremely thin notch (measured
// directly: sweep case 16 "torus+box half torus in box", box face with 2
// holes from the torus's own two profile circles - a real regression
// this shipped with once, caught by this file's own full-sweep
// before/after diff, never released) - IsSimplePolygon() and the area
// check both still passed (neither is sensitive to a merely-very-short,
// not zero-length, edge), yet the resulting trim curve had two
// numerically-coincident consecutive points, an ON_Brep::IsValid()
// failure ("Line points are coincident"). Searching every hole against
// the same pristine `outer` avoids the entanglement outright: distinct,
// well-separated holes get distinct, well-separated attachment points on
// the ORIGINAL boundary, never on each other's own bridge. `reserved`
// additionally blocks two holes from ever choosing the same or an
// adjacent original outer index, as a second, independent guard.
double SignedAreaUV(const std::vector<UVPt>& loop) {
  double area = 0.0;
  const size_t n = loop.size();
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a = loop[i].uv;
    const Point2d& b = loop[(i + 1) % n].uv;
    area += a.x * b.y - b.x * a.y;
  }
  return 0.5 * area;
}

UVPt LerpOnSurface(const ON_Surface* s, const UVPt& a, const UVPt& b, double t) {
  const double u = a.uv.x + (b.uv.x - a.uv.x) * t;
  const double v = a.uv.y + (b.uv.y - a.uv.y) * t;
  return {s->PointAt(u, v), Point2d(u, v)};
}

struct HoleAttachment {
  size_t outer_index = 0;      // original outer[] index this attachment REPLACES (skipped, not kept)
  std::vector<UVPt> insert;    // [a_out, c_in, ...hole walk..., c_out, a_in]
};

std::vector<UVPt> AssembleWithAttachments(const std::vector<UVPt>& outer, const std::vector<HoleAttachment>& attachments) {
  std::unordered_map<size_t, const std::vector<UVPt>*> by_index;
  for (const HoleAttachment& a : attachments) by_index.emplace(a.outer_index, &a.insert);
  std::vector<UVPt> merged;
  merged.reserve(outer.size() * 2);
  for (size_t i = 0; i < outer.size(); ++i) {
    const auto it = by_index.find(i);
    if (it == by_index.end()) {
      merged.push_back(outer[i]);
    } else {
      for (const UVPt& p : *it->second) merged.push_back(p);
    }
  }
  return merged;
}

bool BridgeHolesIntoOuter(std::vector<UVPt>& outer, std::vector<std::vector<UVPt>>& holes, const ON_Surface* surface) {
  if (!surface || outer.size() < 4) return false;
  const size_t n = outer.size();
  const double outer_area = SignedAreaUV(outer);

  // 0.1% of one already-short (densely sampled) edge's own length - the
  // notch corners this produces are, per this function's own doc
  // comment, orders of magnitude above IsConvexPolygon()'s 1e-12
  // collinearity threshold, while the area this sliver costs is many
  // orders of magnitude below the tessellation-scale errors this whole
  // fix exists to close.
  constexpr double kEdgeFraction = 1e-3;

  std::vector<HoleAttachment> attachments;
  std::vector<bool> reserved(n, false);
  std::vector<std::vector<UVPt>> remaining;
  double accepted_hole_area = 0.0;

  for (std::vector<UVPt>& hole : holes) {
    if (hole.size() < 3) {
      remaining.push_back(std::move(hole));
      continue;
    }
    // A closed intersection chain is stored with its own closing point
    // repeated (front and back are the SAME point) - see this file's own
    // top comment on why (the chain's OWN "closed" test needs it) - unlike
    // every other loop this function handles, which use the "N distinct
    // points, implicit wrap" convention throughout. Left alone, walking
    // "every hole point except the one at the bridge" below would still
    // cross that repeated point mid-walk (unless the bridge happens to
    // land exactly on it), inserting the same physical point twice in a
    // row into the merged polygon - a genuine, exact-zero-length
    // coincident-points ON_Brep::IsValid() failure, confirmed directly
    // (sweep case 16 "torus+box half torus in box": Union regressed from
    // valid to invalid on exactly this before this dedup was added).
    // Normalized back to this function's own "no repeated closing point"
    // convention up front, once, before any of the rest of this loop
    // reasons about hole indices at all.
    std::vector<UVPt> hole_pts = hole;
    if (hole_pts.size() >= 2 && (hole_pts.front().p - hole_pts.back().p).Length() <= 1e-9) {
      hole_pts.pop_back();
    }
    if (hole_pts.size() < 3) {
      remaining.push_back(std::move(hole));
      continue;
    }
    const size_t m = hole_pts.size();
    std::vector<UVPt> oriented_hole = hole_pts;
    if ((outer_area >= 0.0) == (SignedAreaUV(hole_pts) >= 0.0)) {
      std::reverse(oriented_hole.begin(), oriented_hole.end());
    }
    const double hole_area = SignedAreaUV(oriented_hole);

    std::vector<std::pair<double, std::pair<size_t, size_t>>> candidates;
    candidates.reserve(n * m);
    for (size_t i = 0; i < n; ++i) {
      for (size_t j = 0; j < m; ++j) candidates.push_back({Dist2(outer[i].uv, oriented_hole[j].uv), {i, j}});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const std::pair<double, std::pair<size_t, size_t>>& a,
                 const std::pair<double, std::pair<size_t, size_t>>& b) { return a.first < b.first; });

    const size_t kMaxTries = std::min<size_t>(candidates.size(), 32);
    const double area_scale = std::max({std::fabs(outer_area), std::fabs(hole_area), 1.0});
    bool accepted = false;

    for (size_t t = 0; t < kMaxTries && !accepted; ++t) {
      const size_t i = candidates[t].second.first;
      const size_t j = candidates[t].second.second;
      if (reserved[i] || reserved[(i + n - 1) % n] || reserved[(i + 1) % n]) continue;

      const UVPt& o_prev = outer[(i + n - 1) % n];
      const UVPt& o_i = outer[i];
      const UVPt& o_next = outer[(i + 1) % n];
      const UVPt& h_prev = oriented_hole[(j + m - 1) % m];
      const UVPt& h_j = oriented_hole[j];
      const UVPt& h_next = oriented_hole[(j + 1) % m];

      HoleAttachment cand;
      cand.outer_index = i;
      cand.insert.reserve(m + 4);
      cand.insert.push_back(LerpOnSurface(surface, o_prev, o_i, 1.0 - kEdgeFraction));
      cand.insert.push_back(LerpOnSurface(surface, h_j, h_next, kEdgeFraction));
      for (size_t k = (j + 1) % m; k != j; k = (k + 1) % m) cand.insert.push_back(oriented_hole[k]);
      cand.insert.push_back(LerpOnSurface(surface, h_prev, h_j, 1.0 - kEdgeFraction));
      cand.insert.push_back(LerpOnSurface(surface, o_i, o_next, kEdgeFraction));

      std::vector<HoleAttachment> trial = attachments;
      trial.push_back(std::move(cand));
      const std::vector<UVPt> merged = AssembleWithAttachments(outer, trial);
      const double expected_area = outer_area + accepted_hole_area + hole_area;

      if (!dino8::kernel::detail::IsSimplePolygon(ToPoly(merged))) continue;
      if (std::fabs(SignedAreaUV(merged) - expected_area) > 1e-3 * area_scale) continue;

      attachments = std::move(trial);
      reserved[i] = true;
      accepted_hole_area += hole_area;
      accepted = true;
    }
    if (!accepted) remaining.push_back(std::move(hole));
  }

  if (attachments.empty()) return false;
  outer = AssembleWithAttachments(outer, attachments);
  holes = std::move(remaining);
  if (std::getenv("DINO8_BOOL_DEBUG")) {
    std::fprintf(stderr, "  BridgeHolesIntoOuter: bridged=%zu leftover_holes=%zu merged_n=%zu\n", attachments.size(),
                 holes.size(), outer.size());
  }
  return true;
}

// --- classification: ray-cast a 3D point against a whole Brep's own
// faces using the general CSX (IntersectCurveSurface), generalizing
// boolean.cpp's own ClassifyPointVsSolid beyond hand-solved formulas. ---
std::vector<Vector3d> GenericRayDirections() {
  const double phi = 1.6180339887498948482;
  const double phi2 = phi * phi;
  std::vector<Vector3d> dirs = {
      Vector3d(1.0, phi, phi2),   Vector3d(phi, phi2, 1.0),   Vector3d(phi2, 1.0, phi),
      Vector3d(1.0, -phi, phi2),  Vector3d(-phi, phi2, 1.0),  Vector3d(phi2, -1.0, -phi),
      Vector3d(-1.0, phi, -phi2), Vector3d(phi, -phi2, 1.0),
  };
  for (Vector3d& d : dirs) {
    const double len = d.Length();
    if (len > 1e-12) d = d / len;
  }
  return dirs;
}

enum class Cls { In, Out };

Cls ClassifyPointVsBrep(const Point3d& p, const ON_Brep& other, double ray_length, const IntersectOptions& opt,
                         double tol) {
  const std::vector<Vector3d> dirs = GenericRayDirections();
  for (const Vector3d& d : dirs) {
    bool clean = true;
    int crossings = 0;
    ON_LineCurve ray(p, p + d * ray_length);
    for (int fi = 0; fi < other.m_F.Count() && clean; ++fi) {
      const ON_BrepFace& face = other.m_F[fi];
      const ON_Surface* s = face.SurfaceOf();
      if (!s) continue;
      ON_BoundingBox fb = s->BoundingBox();
      fb.m_min -= ON_3dVector(tol, tol, tol);
      fb.m_max += ON_3dVector(tol, tol, tol);
      ON_BoundingBox ray_box;
      ray_box.Set(p, false);
      ray_box.Set(p + d * ray_length, true);
      if (fb.IsDisjoint(ray_box)) continue;
      std::vector<CurveSurfaceHit> hits = IntersectCurveSurface(ray, *s, opt);
      for (const CurveSurfaceHit& h : hits) {
        if (h.t <= tol) continue;  // at/behind the ray origin
        if (!FaceContainsUV(face, h.uv.x, h.uv.y)) continue;
        // Grazing hit very near this face's own trim boundary can't be
        // parity-counted reliably - abandon this direction.
        const double eps = 1e-4;
        const bool near_u0 = std::fabs(h.uv.x - s->Domain(0).Min()) < eps || std::fabs(h.uv.x - s->Domain(0).Max()) < eps;
        const bool near_v0 = std::fabs(h.uv.y - s->Domain(1).Min()) < eps || std::fabs(h.uv.y - s->Domain(1).Max()) < eps;
        if ((near_u0 || near_v0) && face.m_li.Count() == 0) {
          // Untrimmed face: a hit exactly on the periodic seam/pole is
          // fine (still one genuine crossing); only true near-tangency
          // (ray nearly parallel to the surface there) is ambiguous, and
          // that is caught below by the small `h.error` check implicitly
          // passing through IntersectCurveSurface's own refinement.
        }
        ++crossings;
      }
    }
    if (clean) return (crossings % 2 == 1) ? Cls::In : Cls::Out;
  }
  return Cls::Out;  // exhausted every direction - default to outside
}

struct KeptFace {
  ON_Surface* surface = nullptr;  // owned (caller deletes after adding, or brep takes ownership)
  bool rev = false;
  std::vector<UVPt> outer;
  std::vector<std::vector<UVPt>> holes;
};

// Collapses consecutive loop points that weld to the SAME vertex. A run of
// such points is normally a literal duplicate (e.g. a spliced chain end
// coinciding with a loop sample) and keeps just its first point. But on a
// singular surface side (a sphere's pole line, every (u, v) along which is
// the same 3D point) a run's first and last points carry genuinely
// DIFFERENT (u, v) - the two ends of the pole line as the loop enters and
// leaves it - and BOTH are kept: the trim arriving at the pole must end at
// the run's first (u, v), the trim leaving it must start at its last, and
// BuildLoop() bridges the two with a singular trim (see there). Keeping
// only the first, as this used to, made the departing trim's own 2D line
// run diagonally across the whole pole line from the arriving side's
// (u, v) - for the sphere's seam-side trim out of the south pole, a
// diagonal from (u_min, v_min) to (u_max, v_min + dv): confirmed as the
// "m_type = seam but m_iso is not N/E/W/S_iso" ON_Brep::IsValid() failure
// on the sphere+box Union/Difference results. The loop is rotated to start
// at a run boundary so no run straddles the array's own wraparound.
// The iso side two (u, v) points share when both sit on one of `srf`'s own
// SINGULAR sides (a pole line) and differ along it - not_iso otherwise
// (either not a singular side at all, or a mere (u, v) duplicate).
ON_Surface::ISO SingularSideIso(const ON_Surface* srf, const Point2d& a, const Point2d& b) {
  if (!srf || Dist2(a, b) <= 1e-18) return ON_Surface::not_iso;
  const ON_Interval du = srf->Domain(0), dv = srf->Domain(1);
  const double eu = 1e-9 * std::max(du.Length(), 1.0), ev = 1e-9 * std::max(dv.Length(), 1.0);
  if (srf->IsSingular(0) && std::fabs(a.y - dv.Min()) <= ev && std::fabs(b.y - dv.Min()) <= ev) return ON_Surface::S_iso;
  if (srf->IsSingular(2) && std::fabs(a.y - dv.Max()) <= ev && std::fabs(b.y - dv.Max()) <= ev) return ON_Surface::N_iso;
  if (srf->IsSingular(3) && std::fabs(a.x - du.Min()) <= eu && std::fabs(b.x - du.Min()) <= eu) return ON_Surface::W_iso;
  if (srf->IsSingular(1) && std::fabs(a.x - du.Max()) <= eu && std::fabs(b.x - du.Max()) <= eu) return ON_Surface::E_iso;
  return ON_Surface::not_iso;
}

void CollapseDuplicateVids(std::vector<UVPt>& loop, VertexWelder& welder, const ON_Surface* srf) {
  const size_t n = loop.size();
  if (n == 0) return;
  std::vector<int> vids(n);
  for (size_t k = 0; k < n; ++k) vids[k] = welder.Weld(loop[k].p);
  size_t start = n;
  for (size_t k = 0; k < n; ++k) {
    if (vids[k] != vids[(k + n - 1) % n]) { start = k; break; }
  }
  std::vector<UVPt> out;
  if (start == n) {  // every point is the same vertex: degenerate, collapses away
    out.push_back(loop[0]);
    loop = std::move(out);
    return;
  }
  size_t k = 0;
  while (k < n) {
    const size_t i0 = (start + k) % n;
    size_t len = 1;
    while (k + len < n && vids[(start + k + len) % n] == vids[i0]) ++len;
    const size_t i1 = (start + k + len - 1) % n;
    out.push_back(loop[i0]);
    // Keep the run's last point too ONLY when BuildLoop() will bridge the
    // pair with a singular trim; a same-vertex pair that merely differs
    // by Newton noise in (u, v) (a spliced chain end coinciding with a
    // loop sample) keeps just its first point, exactly as before, so the
    // trims on either side stay 2D-continuous through that one point.
    if (len > 1 && SingularSideIso(srf, loop[i0].uv, loop[i1].uv) != ON_Surface::not_iso) out.push_back(loop[i1]);
    k += len;
  }
  loop = std::move(out);
}

void BuildLoop(ON_Brep& brep, ON_BrepFace& face, ON_BrepLoop::TYPE type, const std::vector<UVPt>& loop_pts,
               VertexWelder& welder, std::unordered_map<uint64_t, int>& edge_of_pair) {
  const size_t n = loop_pts.size();
  if (n < 3) return;
  ON_BrepLoop& loop = brep.NewLoop(type, face);
  const ON_Surface* srf = face.SurfaceOf();
  for (size_t k = 0; k < n; ++k) {
    const size_t k1 = (k + 1) % n;
    const int vid_from = welder.Weld(loop_pts[k].p);
    const int vid_to = welder.Weld(loop_pts[k1].p);
    if (vid_from == vid_to) {
      // Two consecutive points, one vertex: either a literal duplicate
      // (same (u, v) too - nothing to build) or the two ends of a run
      // along a singular surface side that CollapseDuplicateVids() kept
      // on purpose (see there) - bridged by a genuine singular trim (no
      // edge, both ends the one vertex, a 2D line along that side) so the
      // loop stays 2D-continuous and the trims on either side of the pole
      // keep their own exact iso (u, v).
      const Point2d& a = loop_pts[k].uv;
      const Point2d& b = loop_pts[k1].uv;
      const ON_Surface::ISO iso = SingularSideIso(srf, a, b);
      if (iso == ON_Surface::not_iso) continue;  // not a singular side: a mere duplicate
      auto* c2 = new ON_LineCurve(a, b);
      c2->SetDomain(0.0, 1.0);
      const int c2i = brep.AddTrimCurve(c2);
      brep.NewSingularTrim(brep.m_V[vid_from], loop, iso, c2i);
      continue;
    }
    const uint32_t lo = static_cast<uint32_t>(std::min(vid_from, vid_to));
    const uint32_t hi = static_cast<uint32_t>(std::max(vid_from, vid_to));
    const uint64_t key = (static_cast<uint64_t>(lo) << 32) | hi;
    int edge_index;
    const auto it = edge_of_pair.find(key);
    if (it == edge_of_pair.end()) {
      auto* c3 = new ON_LineCurve(brep.m_V[vid_from].point, brep.m_V[vid_to].point);
      c3->SetDomain(0.0, 1.0);
      const int c3i = brep.AddEdgeCurve(c3);
      ON_BrepEdge& edge = brep.NewEdge(brep.m_V[vid_from], brep.m_V[vid_to], c3i);
      edge.m_tolerance = 0.0;
      edge_index = edge.m_edge_index;
      edge_of_pair.emplace(key, edge_index);
    } else {
      edge_index = it->second;
      if (brep.m_E[edge_index].m_ti.Count() >= 2) {
        throw std::runtime_error(
            "dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or "
            "more fragment loops (non-manifold result) - out of scope; see "
            "boolean_general.h's own disclosed limitations");
      }
    }
    ON_BrepEdge& edge = brep.m_E[edge_index];
    const bool bRev3d = (edge.m_vi[0] != vid_from);
    auto* c2 = new ON_LineCurve(loop_pts[k].uv, loop_pts[k1].uv);
    c2->SetDomain(0.0, 1.0);
    const int c2i = brep.AddTrimCurve(c2);
    brep.NewTrim(edge, bRev3d, loop, c2i);
  }
}

// --- weld an untouched face's own boundary into its freshly-cut
// neighbor's shared identity scope --------------------------------------
//
// ROOT CAUSE (see boolean_general.h's own doc comment for the discovery
// context): every kept fragment's own boundary is built by ONE of two
// paths - a CUT fragment's boundary is spliced together from real
// IntersectionCurve points shared verbatim between the two faces on
// either side of the cut (see this file's own top comment, point 4 -
// "the two sides' welded points coincide exactly"), but an UNTOUCHED
// face's whole boundary - and, just as much, the still-untouched PORTION
// of a partially-cut neighbor's own boundary (e.g. a cut cylinder wall's
// own rim, nowhere near the cut) - comes from FaceBoundaryLoop() instead:
// it resamples that face's own real trim/edge curve at a fixed
// `samples_per_edge` FRACTION of that edge's own 2D parameter domain,
// independently per face. For a boundary two faces genuinely share (e.g.
// a solid cylinder's disk cap and its own wall, built by
// Brep::FromMixedFaces() from the SAME dense point ring - confirmed
// directly, see PlanarFace::notch_begin/notch_count's own doc comment in
// brep.h), each face's own trim is a SEPARATE curve object with its own
// parameterization (the cap's a dense ON_PolylineCurve threaded through
// its own real vertices, domain proportional to VERTEX INDEX; the wall's
// a plain ON_LineCurve in (u, v) whose 3D image is the cylinder's true
// isocurve circle, domain proportional to ANGLE) - so sampling each at
// the same `i / samples_per_edge` fraction lands at a DIFFERENT physical
// angle on each side once `i` is not 0 or `samples_per_edge` (confirmed
// directly: box+cylinder Union's cap/wall rim boundary drifts from a
// matching phase at i=0 to roughly a 0.13 degree-per-step divergence by
// i=23, out of VertexWelder's own kWeldTol). The two faces' own real
// ON_BrepVertex endpoints of that shared run DO still coincide exactly
// (both are literal copies of the same corner point the primitive
// constructor emitted), so the identity mechanism BuildLoop() below
// already correctly shares - VertexWelder + edge_of_pair, one instance,
// used for EVERY kept face uniformly - just never gets the chance: with
// no interior point in common, the run welds into two ADJACENT edges
// with two DIFFERENT interior vertex sets instead of one shared edge with
// one, exactly the "no shared edge to walk" gap ReconcileEdgeTopology
// cannot help with (it only ever visits an edge two faces already agree
// is theirs - TrimCount() == 2 - see its own doc comment).
//
// FIX: before the real welder below ever runs, walk every kept fragment's
// own loop (outer and each hole) and weld ITS points too, with a
// throwaway detector of the same identity (kWeldTol) - purely to find
// which loop POSITIONS are "anchors": already-coincident with some OTHER
// kept face's own loop (this is always true at a shared corner/endpoint,
// which every producer of a fragment boundary - chain-splice or
// FaceBoundaryLoop - already reproduces exactly, being literal copies of
// the same real vertex). Between every pair of anchors, a loop has a
// "run" of purely local, so-far-unshared interior points. When exactly
// two runs from two DIFFERENT kept faces share the same anchor pair, they
// are the two sides of one real physical boundary that just hasn't been
// sampled in common yet - reconcile them to ONE shared, chord-snapped
// interior point set (the same technique ReconcileEdgeTopology already
// uses for tessellated boundaries, adapted here to these pre-tessellation
// fragment loops - a straight 3D chord between the two anchors is exactly
// what BuildLoop() below would build as this run's own edge curve anyway,
// see its own `new ON_LineCurve(vertex_from, vertex_to)`, so snapping onto
// it changes nothing this engine doesn't already do at the per-micro-
// segment level). Each side keeps its own (u, v) at every point it
// already had, gets a linearly-interpolated (u, v) at every point only
// the OTHER side had (between its own nearest original bracketing points -
// the same interpolation BridgeHolesIntoOuter's own LerpOnSurface already
// relies on elsewhere in this file), so both sides' own 2D trim stays
// valid while their 3D points become bit-for-bit identical at every
// shared fraction - guaranteeing the real welder below welds them into
// ONE shared ON_BrepEdge with TrimCount() == 2, at which point
// ReconcileEdgeTopology (already unmodified) can finish the job at
// tessellation time exactly as it already does for a chain-cut edge.
void ReconcileFragmentBoundaries(std::vector<KeptFace>& kept) {
  const bool debug = std::getenv("DINO8_BOOL_DEBUG") != nullptr;
  struct LoopInfo {
    int kf_index;
    std::vector<UVPt>* pts;
    const ON_Surface* surface;
  };
  std::vector<LoopInfo> loops;
  for (size_t ki = 0; ki < kept.size(); ++ki) {
    KeptFace& kf = kept[ki];
    if (kf.outer.size() >= 3) loops.push_back({static_cast<int>(ki), &kf.outer, kf.surface});
    for (std::vector<UVPt>& h : kf.holes) {
      if (h.size() >= 3) loops.push_back({static_cast<int>(ki), &h, kf.surface});
    }
  }
  if (loops.size() < 2) return;

  // Detector pass: weld every point of every loop, and for each resulting
  // id, which kept faces (by index) own at least one point that welds to
  // it - purely to tell an "anchor" (shared with some OTHER face already)
  // from an ordinary local sample.
  VertexWelder detect;
  std::vector<std::vector<int>> vids(loops.size());
  std::unordered_map<int, std::vector<int>> vid_kfs;
  for (size_t li = 0; li < loops.size(); ++li) {
    const std::vector<UVPt>& pts = *loops[li].pts;
    vids[li].resize(pts.size());
    for (size_t k = 0; k < pts.size(); ++k) {
      const int vid = detect.Weld(pts[k].p);
      vids[li][k] = vid;
      std::vector<int>& owners = vid_kfs[vid];
      if (std::find(owners.begin(), owners.end(), loops[li].kf_index) == owners.end()) {
        owners.push_back(loops[li].kf_index);
      }
    }
  }
  auto is_anchor = [&](size_t li, size_t k) { return vid_kfs[vids[li][k]].size() >= 2; };

  // Group every loop's own anchor-to-anchor run by its (undirected) anchor
  // vertex-id pair. Two special cases beyond the ordinary "run between two
  // DIFFERENT anchor positions" below, both confirmed directly on
  // box+cylinder Union's own cap/wall boundary:
  //   - A loop with only ONE anchor position at all (an untouched face's
  //     whole boundary touches its neighbor at exactly one real vertex,
  //     e.g. a solid cylinder's disk cap meeting its own wall only at the
  //     rim's own single angle-0 corner - FromMixedFaces() gives the cap
  //     and the wall separate edge objects for their shared rim, see this
  //     function's own doc comment, but DOES weld their shared corner
  //     vertex) - its own single run is the WHOLE loop, wrapping from that
  //     one anchor position all the way around back to itself.
  //   - Two DIFFERENT positions on the SAME loop that happen to weld to
  //     the SAME anchor vertex (the neighboring wall fragment's own view
  //     of that identical rim: its loop leaves and later returns to that
  //     same corner vertex, since it ALSO continues on past it toward a
  //     completely different, unrelated shared edge - the freshly-cut
  //     chain boundary against a third face). This run's own two ends
  //     share one vertex id, not two - `key` below uses that one id
  //     twice, deliberately not skipped as a trivial same-point span.
  struct Run {
    size_t li;
    size_t a_pos, b_pos;  // loop-local indices of this run's own two anchors
  };
  std::map<std::pair<int, int>, std::vector<Run>> runs_by_pair;
  for (size_t li = 0; li < loops.size(); ++li) {
    const size_t n = loops[li].pts->size();
    std::vector<size_t> anchors;
    for (size_t k = 0; k < n; ++k) {
      if (is_anchor(li, k)) anchors.push_back(k);
    }
    if (debug) {
      std::fprintf(stderr, "  loop li=%zu kf=%d n=%zu anchors=%zu:", li, loops[li].kf_index, n, anchors.size());
      for (size_t a : anchors) std::fprintf(stderr, " [%zu vid=%d]", a, vids[li][a]);
      std::fprintf(stderr, "\n");
    }
    if (anchors.empty()) continue;
    if (anchors.size() == 1) {
      const int vid = vids[li][anchors[0]];
      runs_by_pair[{vid, vid}].push_back({li, anchors[0], anchors[0]});
      continue;
    }
    for (size_t ai = 0; ai < anchors.size(); ++ai) {
      const size_t a_pos = anchors[ai];
      const size_t b_pos = anchors[(ai + 1) % anchors.size()];
      const int vid_a = vids[li][a_pos], vid_b = vids[li][b_pos];
      const std::pair<int, int> key(std::min(vid_a, vid_b), std::max(vid_a, vid_b));
      runs_by_pair[key].push_back({li, a_pos, b_pos});
    }
  }

  // A run's own points, in loop order from a_pos to b_pos inclusive
  // (circularly - walking the WHOLE loop and back to a_pos again when
  // a_pos == b_pos, the single-anchor case above), plus each point's own
  // fraction of the run's total 3D ARC LENGTH (0 at a_pos, 1 at b_pos) -
  // not a straight-chord projection: a single-anchor run has no second,
  // distinct endpoint to define a chord from, and arc length remains
  // perfectly well-defined (and equally valid for an ordinary open run)
  // regardless.
  struct RunPoint {
    Point3d p;
    Point2d uv;
    double t;
  };
  struct GatherResult {
    std::vector<RunPoint> pts;
    double total_len = 0.0;  // the run's own real 3D arc length, BEFORE t-normalization
  };
  auto gather = [&](const Run& r) {
    const std::vector<UVPt>& pts = *loops[r.li].pts;
    const size_t n = pts.size();
    std::vector<size_t> idx;
    if (r.a_pos == r.b_pos) {
      for (size_t step = 0; step < n; ++step) idx.push_back((r.a_pos + step) % n);
      idx.push_back(r.a_pos);
    } else {
      for (size_t k = r.a_pos; k != r.b_pos; k = (k + 1) % n) idx.push_back(k);
      idx.push_back(r.b_pos);
    }
    GatherResult res;
    res.pts.reserve(idx.size());
    double total = 0.0;
    res.pts.push_back({pts[idx[0]].p, pts[idx[0]].uv, 0.0});
    for (size_t k = 1; k < idx.size(); ++k) {
      const Point3d& prev = pts[idx[k - 1]].p;
      const Point3d& cur = pts[idx[k]].p;
      total += std::sqrt((cur.x - prev.x) * (cur.x - prev.x) + (cur.y - prev.y) * (cur.y - prev.y) +
                          (cur.z - prev.z) * (cur.z - prev.z));
      res.pts.push_back({cur, pts[idx[k]].uv, total});
    }
    res.total_len = total;
    if (total > 1e-12) {
      for (RunPoint& rp : res.pts) rp.t /= total;
    }
    return res;
  };

  // Linear-in-arc-length-fraction interpolation of `arr`'s own (p, uv) at
  // fraction `t` (already normalized the same way `gather()` above
  // computes its own points' `t`).
  auto interp_at = [](const std::vector<RunPoint>& arr, double t) {
    size_t lo = 0;
    while (lo + 2 < arr.size() && arr[lo + 1].t <= t) ++lo;
    const RunPoint& a = arr[lo];
    const RunPoint& b = arr[std::min(lo + 1, arr.size() - 1)];
    const double span = b.t - a.t;
    const double frac = std::fabs(span) > 1e-15 ? (t - a.t) / span : 0.0;
    UVPt out;
    out.p = Point3d(a.p.x + (b.p.x - a.p.x) * frac, a.p.y + (b.p.y - a.p.y) * frac, a.p.z + (b.p.z - a.p.z) * frac);
    out.uv = Point2d(a.uv.x + (b.uv.x - a.uv.x) * frac, a.uv.y + (b.uv.y - a.uv.y) * frac);
    return out;
  };

  // Phase 1: decide every run's own new interior point set, purely by
  // reading the ORIGINAL (pre-edit) loop point arrays - never mutating a
  // loop here, since a loop can carry several independent runs (e.g. a
  // cut cylinder wall's own untouched rim AND its freshly-cut far edge)
  // and rewriting one in place would invalidate every other run's own
  // `a_pos`/`b_pos` loop-local indices into the same array.
  struct Edit {
    size_t a_pos, b_pos;
    std::vector<UVPt> new_interior;  // NOT including the endpoints at a_pos/b_pos
  };
  std::vector<std::vector<Edit>> edits(loops.size());

  if (debug) {
    int total_pairs = 0, two_run_pairs = 0, cross_face_pairs = 0;
    for (auto& [key, rs] : runs_by_pair) {
      ++total_pairs;
      if (rs.size() == 2) {
        ++two_run_pairs;
        if (loops[rs[0].li].kf_index != loops[rs[1].li].kf_index) ++cross_face_pairs;
      }
    }
    std::fprintf(stderr, "ReconcileFragmentBoundaries: loops=%zu anchor_pairs=%d two_run=%d cross_face=%d\n",
                 loops.size(), total_pairs, two_run_pairs, cross_face_pairs);
  }
  auto dist2 = [](const Point3d& x, const Point3d& y) {
    const double dx = x.x - y.x, dy = x.y - y.y, dz = x.z - y.z;
    return dx * dx + dy * dy + dz * dz;
  };
  for (auto& [key, rs] : runs_by_pair) {
    if (rs.size() != 2) continue;  // ambiguous (0, 1, or 3+ claimants) - leave alone
    if (loops[rs[0].li].kf_index == loops[rs[1].li].kf_index) continue;  // same face's own seam - out of scope

    const GatherResult ga = gather(rs[0]);
    const GatherResult gb = gather(rs[1]);
    if (ga.pts.size() <= 2 && gb.pts.size() <= 2) continue;  // both sides already a single direct span

    // The denser side is the ground truth: its own points are reused
    // VERBATIM (bit-for-bit) as the sparse side's new interior points, so
    // the real welder below is guaranteed to merge them into one shared
    // vertex per point, not just within tolerance. The reference side
    // itself is left completely untouched.
    const bool a_is_ref = ga.pts.size() >= gb.pts.size();
    const std::vector<RunPoint>& ref = a_is_ref ? ga.pts : gb.pts;
    const std::vector<RunPoint>& sparse_full = a_is_ref ? gb.pts : ga.pts;
    const Run& sparse_run = a_is_ref ? rs[1] : rs[0];
    if (ref.size() <= 2) continue;  // reference itself has no interior points to share

    // `ref`'s own t and `sparse_full`'s own t need not run the same
    // direction: two ADJACENT faces of a manifold solid trace their one
    // shared boundary in OPPOSITE senses (this file's own outward-CCW
    // convention - see e.g. MakeBoxXform's own "CCW as seen from outside"
    // loops), so a_pos need not land on the same PHYSICAL end on both
    // sides - confirmed directly as a real, not theoretical, bug: naively
    // assuming matching direction sent a wall fragment's own (u, v) up to
    // 0.51 units (of a radius-1 cylinder) from its true point, an
    // ON_Brep::IsValid() failure.
    //
    // Both this AND direction itself are settled together by the SAME
    // multi-probe geometric vote, not assumed: an anchor-vertex pair can
    // be shared by two loops that DON'T trace the same physical curve at
    // all (e.g. two otherwise-unrelated fragments that merely touch at
    // one corner point, each with no OTHER shared vertex of its own - the
    // exact shape a genuine disk-cap/wall run also has) - confirmed
    // directly as a real, not theoretical, false-positive: an early,
    // single-probe version of this same check accepted several such
    // unrelated pairs across the sweep's OTHER cases (cyl+cyl, sphere+
    // sphere, box+cone - none involving an untouched disk cap at all),
    // each a real WRONG-VOLUME or non-manifold regression measured
    // directly, not one this file's own comment can just assert away.
    // Three probes, well clear of both t=0.5 (ambiguous on a symmetric
    // curve) and the endpoints, each required to land within a real
    // geometric tolerance (scaled off the SPARSE run's own arc length,
    // the shorter/less precise of this pair) of `ref`'s corresponding
    // point, in whichever of the two candidate directions wins the FIRST
    // probe - reject the whole pair the moment any probe disagrees.
    constexpr double kProbes[3] = {0.25, 0.5, 0.75};
    const double probe_tol = std::max(1e-6, 0.05 * std::min(ga.total_len, gb.total_len));
    bool same_direction = true;
    bool accepted = true;
    for (int pi = 0; pi < 3 && accepted; ++pi) {
      const double t = kProbes[pi];
      const Point3d ref_probe = interp_at(ref, t).p;
      const Point3d same_probe = interp_at(sparse_full, t).p;
      const Point3d flip_probe = interp_at(sparse_full, 1.0 - t).p;
      const double d_same = dist2(same_probe, ref_probe);
      const double d_flip = dist2(flip_probe, ref_probe);
      if (pi == 0) same_direction = d_same <= d_flip;
      const double d = same_direction ? d_same : d_flip;
      if (d > probe_tol * probe_tol) accepted = false;
    }
    if (!accepted) {
      if (debug) {
        std::fprintf(stderr, "  REJECTED key=(%d,%d) kf_a=%d kf_b=%d ga=%zu gb=%zu (probe mismatch - not the same curve)\n",
                     key.first, key.second, loops[rs[0].li].kf_index, loops[rs[1].li].kf_index, ga.pts.size(), gb.pts.size());
      }
      continue;
    }

    // Built walking `ref` in ITS OWN forward (a_pos -> b_pos) order, each
    // point's `p` taken verbatim from `ref` and `uv` interpolated for the
    // matching physical location on the sparse side. When the two runs
    // trace the shared curve in OPPOSITE physical senses (`!same_direction`
    // - the common case for two ADJACENT faces, see above), `ref`'s own
    // forward order is the SPARSE side's own BACKWARD order (b_pos ->
    // a_pos) - confirmed directly as a real, not theoretical, bug: left
    // unreversed, this spliced a wall fragment's own new interior points
    // in from its rim end (b_pos) toward its start (a_pos), backwards
    // against the rest of its own loop, corrupting the whole loop's
    // winding without the earlier direction/probe checks having any way
    // to catch it (both check per-point CORRESPONDENCE, not the run's own
    // insertion order) - measured as a genuine ~10% WRONG-VOLUME, not
    // merely a slower convergence. Reversed here so the spliced sequence
    // always runs from near a_pos to near b_pos in the SPARSE loop's own
    // forward sense, matching every other run this function ever builds.
    std::vector<UVPt> new_interior;
    new_interior.reserve(ref.size() - 2);
    for (size_t k = 1; k + 1 < ref.size(); ++k) {
      const double query_t = same_direction ? ref[k].t : 1.0 - ref[k].t;
      new_interior.push_back({ref[k].p, interp_at(sparse_full, query_t).uv});
    }
    if (!same_direction) std::reverse(new_interior.begin(), new_interior.end());
    edits[sparse_run.li].push_back({sparse_run.a_pos, sparse_run.b_pos, std::move(new_interior)});
    if (debug) {
      std::fprintf(stderr, "  reconciled key=(%d,%d) kf_a=%d kf_b=%d ga=%zu gb=%zu ref=%s\n", key.first, key.second,
                   loops[rs[0].li].kf_index, loops[rs[1].li].kf_index, ga.pts.size(), gb.pts.size(),
                   a_is_ref ? "a" : "b");
    }
  }

  // Phase 2: apply every loop's own edits (there may be several,
  // disjoint by construction - each covers one run between two distinct
  // anchors) in one single rebuild pass, so no edit's own a_pos/b_pos
  // ever refers to an already-mutated array.
  for (size_t li = 0; li < loops.size(); ++li) {
    if (edits[li].empty()) continue;
    std::vector<UVPt>& pts = *loops[li].pts;
    const size_t n = pts.size();
    std::vector<bool> skip(n, false);
    std::unordered_map<size_t, const std::vector<UVPt>*> insert_after;
    for (const Edit& e : edits[li]) {
      for (size_t k = (e.a_pos + 1) % n; k != e.b_pos; k = (k + 1) % n) skip[k] = true;
      insert_after[e.a_pos] = &e.new_interior;
      if (debug) {
        std::fprintf(stderr, "  EDIT li=%zu kf=%d a_pos=%zu b_pos=%zu new_interior=%zu:", li, loops[li].kf_index,
                     e.a_pos, e.b_pos, e.new_interior.size());
        for (const UVPt& p : e.new_interior)
          std::fprintf(stderr, " (uv=%.4f,%.4f p=%.4f,%.4f,%.4f)", p.uv.x, p.uv.y, p.p.x, p.p.y, p.p.z);
        std::fprintf(stderr, "\n");
      }
    }
    std::vector<UVPt> result;
    result.reserve(n + 8);
    for (size_t k = 0; k < n; ++k) {
      if (!skip[k]) result.push_back(pts[k]);
      const auto it = insert_after.find(k);
      if (it != insert_after.end()) result.insert(result.end(), it->second->begin(), it->second->end());
    }
    pts = std::move(result);
  }
}

}  // namespace

Brep BooleanCombineGeneral(const Brep& a, const Brep& b, BooleanOp op) {
  if (op == BooleanOp::SymmetricDifference) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineGeneral: SymmetricDifference is not "
        "yet implemented - see boolean_general.h's own disclosed scope");
  }

  const ON_Brep& ba = a.raw();
  const ON_Brep& bb = b.raw();
  IntersectOptions opt;

  const BoundingBox tbb_a = a.GetTightBoundingBox();
  const BoundingBox tbb_b = b.GetTightBoundingBox();
  const ON_BoundingBox bbox_a(tbb_a.min, tbb_a.max);
  const ON_BoundingBox bbox_b(tbb_b.min, tbb_b.max);
  const double ray_length = 4.0 * (bbox_a.Diagonal().Length() + bbox_b.Diagonal().Length() + 1.0);
  const double tol = 1e-6;

  const int na = ba.m_F.Count();
  const int nb = bb.m_F.Count();

  // Every chain gathered here is only a PIECE of a face's own true
  // boundary curve where more than one opposing face meets it (see
  // StitchChains's own doc comment) - stitched into maximal chains below,
  // per face, before any open/closed classification is attempted.
  std::vector<std::vector<Chain>> raw_a(static_cast<size_t>(na)), raw_b(static_cast<size_t>(nb));

  std::vector<ON_BoundingBox> boxes_a(static_cast<size_t>(na)), boxes_b(static_cast<size_t>(nb));
  for (int i = 0; i < na; ++i) boxes_a[static_cast<size_t>(i)] = ba.m_F[i].SurfaceOf()->BoundingBox();
  for (int j = 0; j < nb; ++j) boxes_b[static_cast<size_t>(j)] = bb.m_F[j].SurfaceOf()->BoundingBox();

  // Coincident-face bookkeeping (see this file's own top-of-file doc
  // comment, "coplanar shared face" fix): a face of A and a face of B that
  // lie on the EXACT same plane with the EXACT same finite extent (both
  // planar, same offset + parallel normal, matching bounding boxes) never
  // produce an SSX curve at all - coincident surfaces have no proper
  // transversal intersection, so IntersectFaces() correctly returns zero
  // curves for the pair - but the two faces are still one PHYSICAL surface
  // shared by both solids, and ray-cast classification of a point sitting
  // exactly ON that shared plane is numerically arbitrary (it can come
  // back In or Out from either side essentially at random). Recorded here,
  // keyed by face index on each side, with whether the two faces' outward
  // normals agree (same_normal, e.g. two overlapping prisms sharing an
  // exact top plane - boolean.cpp's BooleanCombinePlanar/BooleanCombineMixed
  // dedup this by keeping exactly one copy) or oppose (opposite_normal,
  // e.g. this fix's own fixture: two boxes merely touching face-to-face,
  // filling opposite sides of that one shared plane with no volumetric
  // overlap at all - see the override applied in `process` below for why
  // opposing normals need a DIFFERENT rule Union/Intersection never needed
  // before, one boolean.cpp itself has never had a test exercise either).
  struct CoincidentInfo {
    int other_face = -1;
    bool opposite_normal = false;
  };
  std::vector<std::vector<CoincidentInfo>> coincident_a(static_cast<size_t>(na)), coincident_b(static_cast<size_t>(nb));
  const bool debug = std::getenv("DINO8_BOOL_DEBUG") != nullptr;

  for (int i = 0; i < na; ++i) {
    ON_BoundingBox exp_a = boxes_a[static_cast<size_t>(i)];
    exp_a.m_min -= ON_3dVector(tol, tol, tol);
    exp_a.m_max += ON_3dVector(tol, tol, tol);
    for (int j = 0; j < nb; ++j) {
      if (exp_a.IsDisjoint(boxes_b[static_cast<size_t>(j)])) continue;
      const ON_BrepFace& fa = ba.m_F[i];
      const ON_BrepFace& fb = bb.m_F[j];
      std::vector<IntersectionCurve> curves = IntersectFaces(&fa, *fa.SurfaceOf(), &fb, *fb.SurfaceOf(), opt);
      if (curves.empty()) {
        ON_Plane pa, pb;
        const ON_Surface* sa = fa.SurfaceOf();
        const ON_Surface* sb = fb.SurfaceOf();
        if (sa->IsPlanar(&pa, tol) && sb->IsPlanar(&pb, tol)) {
          const int parallel = pa.zaxis.IsParallelTo(pb.zaxis, 1e-6);
          if (parallel != 0 && std::fabs(pa.DistanceTo(pb.origin)) <= tol) {
            const ON_BoundingBox& box_a = boxes_a[static_cast<size_t>(i)];
            const ON_BoundingBox& box_b = boxes_b[static_cast<size_t>(j)];
            const double extent_tol = std::max(tol, 1e-6 * std::max(box_a.Diagonal().Length(), box_b.Diagonal().Length()));
            if (box_a.m_min.DistanceTo(box_b.m_min) <= extent_tol && box_a.m_max.DistanceTo(box_b.m_max) <= extent_tol) {
              coincident_a[static_cast<size_t>(i)].push_back({j, parallel == -1});
              coincident_b[static_cast<size_t>(j)].push_back({i, parallel == -1});
              if (debug) std::fprintf(stderr, "coincident face pair: A[%d] <-> B[%d], opposite_normal=%d\n", i, j, parallel == -1);
            }
          }
        }
      }
      for (const IntersectionCurve& ic : curves) {
        if (ic.points.size() < 2) continue;
        Chain ca, cb;
        ca.reserve(ic.points.size() + 1);
        cb.reserve(ic.points.size() + 1);
        for (size_t k = 0; k < ic.points.size(); ++k) {
          ca.push_back({ic.points[k], ic.uv_a[k]});
          cb.push_back({ic.points[k], ic.uv_b[k]});
        }
        // IntersectFaces() already told us this curve is a closed loop
        // (IntersectionCurve::closed) - it just doesn't repeat the closing
        // point (the same "N distinct points, implicit wrap" convention
        // every closed curve in this module uses). This file's own
        // open/closed test below - and StitchChains's own "already closed"
        // check - both work purely by comparing a Chain's OWN front and
        // back 3D points, which only agree for a curve that already IS a
        // repeated-endpoint loop; without re-adding that endpoint here, an
        // intersection curve that IntersectFaces() returns as a complete
        // closed loop in a SINGLE piece (needing no stitching with any
        // other face-pair's own piece at all) has its first and last
        // samples merely ADJACENT points on the loop - one mesh cell
        // apart, nowhere near within `stitch_tol` - so it was silently
        // misclassified as a wide-open chain and then dropped by
        // SplitFaceLoop (its two "ends" don't sit on any real trim
        // boundary). Confirmed root cause of the box-fully-pierced-by-a-
        // cylinder case (see this file's own top-of-file doc comment).
        if (ic.closed) {
          ca.push_back(ca.front());
          cb.push_back(cb.front());
        }
        raw_a[static_cast<size_t>(i)].push_back(std::move(ca));
        raw_b[static_cast<size_t>(j)].push_back(std::move(cb));
      }
    }
  }

  // Two different SSX curves (this face against two different opposing
  // faces) that meet at the same true 3D corner are each independently
  // Newton-refined to `opt.tolerance`, so their shared endpoint generally
  // isn't bit-identical between the two - stitch with real slack, not
  // kWeldTol (that tight tolerance is for the FINAL assembly, where a
  // chain's own points are shared verbatim across its own two faces, not
  // independently re-solved).
  const double stitch_tol = std::max(1e-4, opt.tolerance * 20.0);

  // Whether a face was touched by ANY SSX curve at all (captured before
  // `raw_a`/`raw_b` are moved-from below, one face at a time, inside
  // `build_frags`). An untouched planar face that is also part of a
  // `coincident_a`/`coincident_b` pair is exactly the "whole shared face,
  // never split" fixture the coincident-face override below applies to -
  // a face touched by even one real SSX curve (a genuine partial overlap,
  // not full coincidence) is deliberately excluded from that override and
  // left to the ordinary ray-cast classification path.
  std::vector<bool> untouched_a(static_cast<size_t>(na)), untouched_b(static_cast<size_t>(nb));
  for (int i = 0; i < na; ++i) untouched_a[static_cast<size_t>(i)] = raw_a[static_cast<size_t>(i)].empty();
  for (int j = 0; j < nb; ++j) untouched_b[static_cast<size_t>(j)] = raw_b[static_cast<size_t>(j)].empty();

  // Fragment every face of both operands.
  struct FaceFrags {
    int face_index = 0;
    ON_Surface* surface = nullptr;
    bool base_rev = false;
    std::vector<Fragment> frags;
  };
  auto build_frags = [&](const ON_Brep& brep, int n, std::vector<std::vector<Chain>>& raw) {
    std::vector<FaceFrags> out;
    for (int i = 0; i < n; ++i) {
      std::vector<UVPt> boundary = FaceBoundaryLoop(brep, i);
      if (boundary.size() < 3) continue;
      const std::vector<Chain> stitched = StitchChains(std::move(raw[static_cast<size_t>(i)]), stitch_tol);
      const ON_Surface* face_surface = brep.m_F[i].SurfaceOf();
      const bool untrimmed = brep.m_F[i].m_li.Count() == 0;
      std::vector<Chain> closed_chains, open_chains;
      for (const Chain& c : stitched) {
        // A chain running along an untrimmed face's own seam/pole side is
        // cut there first (see CutChainAtDomainBoundary's own doc comment),
        // BEFORE the 3D-closed test below: in (u, v) it is not a closed
        // island at all, and it must not be holed out as one. The
        // on-boundary tolerance is the SSX's own accuracy: a chain point
        // can't be told apart from the seam it sits on any better than
        // IntersectFaces() itself resolved it.
        if (untrimmed && face_surface) {
          std::vector<Chain> cut;
          if (CutChainAtDomainBoundary(c, *face_surface, opt.tolerance, boundary, cut)) {
            if (debug) std::fprintf(stderr, "  face idx=%d: chain n=%zu cut at domain boundary into %zu open chain(s), boundary now %zu\n", i, c.size(), cut.size(), boundary.size());
            for (Chain& oc : cut) open_chains.push_back(std::move(oc));
            continue;
          }
        }
        if ((c.front().p - c.back().p).Length() <= stitch_tol) {
          Chain wrap_open;
          if (face_surface && SplitPeriodicWrapChain(c, *face_surface, wrap_open)) {
            open_chains.push_back(std::move(wrap_open));
          } else {
            closed_chains.push_back(c);
          }
        } else {
          open_chains.push_back(c);
        }
      }
      if (debug) {
        std::fprintf(stderr, "  build_frags face idx=%d boundary=%zu stitched=%zu closed=%zu open=%zu\n", i,
                     boundary.size(), stitched.size(), closed_chains.size(), open_chains.size());
        for (const Chain& c : open_chains)
          std::fprintf(stderr, "    open chain: n=%zu front_uv=(%f,%f) back_uv=(%f,%f)\n", c.size(), c.front().uv.x,
                       c.front().uv.y, c.back().uv.x, c.back().uv.y);
      }
      FaceFrags ff;
      ff.face_index = i;
      ff.surface = brep.m_F[i].SurfaceOf()->DuplicateSurface();
      ff.base_rev = brep.m_F[i].m_bRev;
      ff.frags = SplitFaceLoop(boundary, closed_chains, open_chains);
      out.push_back(std::move(ff));
    }
    return out;
  };
  std::vector<FaceFrags> frags_a = build_frags(ba, na, raw_a);
  std::vector<FaceFrags> frags_b = build_frags(bb, nb, raw_b);

  // Classify + keep, per operation.
  std::vector<KeptFace> kept;
  auto process = [&](std::vector<FaceFrags>& frags, const ON_Brep& other, bool a_side) {
    for (FaceFrags& ff : frags) {
      if (debug) std::fprintf(stderr, "face(%s) idx=%d frags=%zu\n", a_side ? "A" : "B", ff.face_index, ff.frags.size());
      const std::vector<CoincidentInfo>& coincident_here =
          a_side ? coincident_a[static_cast<size_t>(ff.face_index)] : coincident_b[static_cast<size_t>(ff.face_index)];
      // The override below only ever applies to a face that IS its own
      // single, untouched fragment - the "whole shared face, no SSX curve
      // anywhere on it" fixture `coincident_a`/`coincident_b` was built
      // for. A face that also has some OTHER, genuinely intersecting
      // opposing face (so ff.frags.size() != 1, or raw_a/raw_b for it
      // wasn't empty) is left to the ordinary ray-cast path below even if
      // it happens to be coincident with one particular opposing face -
      // out of scope for this fix, same as boolean.cpp's own coincident-
      // plane dedup only ever handling the whole-face case.
      const bool whole_face_untouched =
          !coincident_here.empty() && ff.frags.size() == 1 &&
          (a_side ? untouched_a[static_cast<size_t>(ff.face_index)] : untouched_b[static_cast<size_t>(ff.face_index)]);
      for (Fragment& frag : ff.frags) {
        bool keep = false;
        bool flip = false;
        if (whole_face_untouched) {
          // Coincident-face rule (see this file's own top-of-file doc
          // comment and `coincident_a`/`coincident_b`'s own doc comment
          // above): this fragment IS the entire physical face, and it has
          // an exact coincident twin on the other solid - ray-casting its
          // representative point (which sits exactly ON the other
          // solid's own boundary) would be numerically arbitrary, so skip
          // ClassifyPointVsBrep entirely and decide from the two faces'
          // outward-normal relationship instead, mirroring
          // BooleanCombinePlanar/BooleanCombineMixed's own same_plane
          // dedup convention (boolean.cpp) with one addition theirs never
          // needed: the OPPOSITE-normal case (this fixture's own two
          // boxes merely touching face-to-face, no volumetric overlap).
          //   - opposite normals (touching, not overlapping): the shared
          //     face is interior to the Union (material fills both
          //     sides) and contributes no volume to the Intersection (a
          //     2D contact, not a 3D overlap) - dropped by BOTH sides for
          //     Union and Intersection. For Difference, A's own copy is a
          //     genuine remaining boundary of A - B (B is being removed
          //     from the OTHER side of this same plane, so A's face still
          //     separates A's material from empty space) - kept unflipped
          //     on the `a_side` (the solid named first in THIS call, per
          //     boolean.h's own Difference = a-side convention) and always
          //     dropped on the other side, exactly like boolean.cpp's own
          //     Difference rule for this normal relationship.
          //   - same normals (genuine volumetric overlap sharing an exact
          //     boundary plane, e.g. two overlapping prisms with the same
          //     top height): the pre-existing boolean.cpp convention -
          //     keep exactly one copy for Union/Intersection (the a_side's,
          //     arbitrarily but consistently), cancel both for Difference
          //     (subtracting B removes the coincident material too).
          const bool opposite = coincident_here.front().opposite_normal;
          if (a_side) {
            switch (op) {
              case BooleanOp::Union:
              case BooleanOp::Intersection:
                keep = !opposite;  // same normal: keep one copy (a_side's); opposite: drop both
                break;
              case BooleanOp::Difference:
                keep = opposite;  // opposite: a_side's copy is a genuine remaining boundary; same: cancels
                break;
              default:
                break;
            }
          } else {
            keep = false;  // the other side's coincident copy is always redundant
          }
          if (debug)
            std::fprintf(stderr, "  frag: COINCIDENT face override a_side=%d opposite=%d op=%d -> keep=%d\n", a_side,
                         opposite, static_cast<int>(op), keep);
        } else {
          Point2d uv;
          if (!RepresentativeUV(frag, uv)) {
            if (debug) std::fprintf(stderr, "  frag: NO representative UV found (outer pts=%zu, holes=%zu)\n", frag.outer.size(), frag.holes.size());
            continue;
          }
          const Point3d p3 = ff.surface->PointAt(uv.x, uv.y);
          const Cls cls = ClassifyPointVsBrep(p3, other, ray_length, opt, tol);
          if (debug) std::fprintf(stderr, "  frag: outer=%zu holes=%zu uv=(%f,%f) p3=(%f,%f,%f) cls=%s\n", frag.outer.size(), frag.holes.size(), uv.x, uv.y, p3.x, p3.y, p3.z, cls == Cls::In ? "In" : "Out");
          switch (op) {
            case BooleanOp::Union:
              keep = (cls == Cls::Out);
              break;
            case BooleanOp::Intersection:
              keep = (cls == Cls::In);
              break;
            case BooleanOp::Difference:
              if (a_side) {
                keep = (cls == Cls::Out);
              } else {
                keep = (cls == Cls::In);
                flip = true;
              }
              break;
            default:
              break;
          }
        }
        if (!keep) continue;
        KeptFace kf;
        kf.surface = ff.surface->DuplicateSurface();
        kf.rev = flip ? !ff.base_rev : ff.base_rev;
        kf.outer = frag.outer;
        kf.holes = frag.holes;
        // Try to fold every hole into kf.outer as a keyhole notch (see
        // BridgeHolesIntoOuter's own doc comment) so the assembled face
        // reaches brep.cpp's exact-clip tessellation path instead of the
        // much cruder whole-cell fallback ANY ON_BrepLoop::inner hole
        // forces it into - the confirmed root cause of sweep case 07's
        // Union/A-B WRONG-VOLUME result. Purely additive: a hole that
        // can't be bridged safely (checked directly, never assumed) stays
        // in kf.holes and is built as an ordinary inner loop below, same
        // as before this fix.
        if (!kf.holes.empty()) BridgeHolesIntoOuter(kf.outer, kf.holes, ff.surface);
        kept.push_back(std::move(kf));
      }
    }
  };
  process(frags_a, bb, true);
  process(frags_b, ba, false);

  // Every FaceFrags::surface was only ever a read-only source for
  // KeptFace::surface's own DuplicateSurface() copies above - free them
  // all now regardless of whether any fragment of that face was kept.
  for (FaceFrags& ff : frags_a) delete ff.surface;
  for (FaceFrags& ff : frags_b) delete ff.surface;

  if (kept.empty()) {
    // The empty solid - degenerate but not an error (e.g. Intersection of
    // two disjoint solids, or a Difference that fully removes `a`).
    for (KeptFace& kf : kept) delete kf.surface;
    return Brep();
  }

  // Weld an untouched face's own boundary (or a cut face's own still-
  // untouched portion) into the same edge-identity scope its freshly-cut
  // neighbor already uses - see ReconcileFragmentBoundaries()'s own doc
  // comment. Purely additive: it only ever adds points strictly between
  // two already-shared anchor vertices, on faces that already share those
  // two anchors exactly, so it cannot change which fragments got kept or
  // any fragment's own overall shape/area.
  ReconcileFragmentBoundaries(kept);

  // Reassemble.
  Brep result;
  ON_Brep& brep = result.raw();
  VertexWelder welder;
  for (KeptFace& kf : kept) {
    CollapseDuplicateVids(kf.outer, welder, kf.surface);
    for (auto& h : kf.holes) CollapseDuplicateVids(h, welder, kf.surface);
  }
  for (const Point3d& p : welder.Points()) brep.NewVertex(p);

  std::unordered_map<uint64_t, int> edge_of_pair;
  for (KeptFace& kf : kept) {
    if (kf.outer.size() < 3) {
      // Degenerate sliver (collapsed to < 3 unique vertices after
      // welding) - AddSurface() never took ownership, free it here.
      delete kf.surface;
      continue;
    }
    const int surface_index = brep.AddSurface(kf.surface);
    ON_BrepFace& face = brep.NewFace(surface_index);
    face.m_bRev = kf.rev;
    BuildLoop(brep, face, ON_BrepLoop::outer, kf.outer, welder, edge_of_pair);
    for (const std::vector<UVPt>& h : kf.holes) {
      if (h.size() >= 3) BuildLoop(brep, face, ON_BrepLoop::inner, h, welder, edge_of_pair);
    }
  }

  brep.SetTrimIsoFlags();
  brep.SetTolerancesBoxesAndFlags(/*bLazy=*/true);
  return result;
}

// --- TessellateGeneralBooleanClosedMesh(): T-junction stitching --------
//
// ROOT CAUSE (measured directly - see tests/scratch_test.cpp's own
// DiagnoseManifold() dump, run on box+box/box+cylinder/sphere+box): this
// file's own edges ARE literal shared ON_BrepEdge objects between the two
// fragments on either side of a cut (confirmed - BuildLoop() above keys
// edges by welded-vertex-id pair and throws if a third loop ever claims
// one), so the *topology* is already correctly shared. The mismatch is
// introduced one step later, purely in TESSELLATION: Brep::Tessellate()
// resolves each face's own trim polygon (brep.cpp's ResolveFace(), via
// SampleLoop() - which, for this engine's straight polyline trims,
// correctly reproduces every polyline vertex exactly, one per trim) and
// then hands it to NurbsSurface::TessellateGridClippedExact(), which grids
// each face's OWN (u, v) domain independently and clips each grid cell
// against that polygon - inserting a NEW boundary point wherever a grid
// line crosses the polygon boundary. Two faces sharing one polyline edge
// almost never have the same (u, v) domain/grid orientation (a box's top
// face and side face, say, or a cylinder wall's own periodic u versus a
// box face's planar u), so their two independently-computed sets of
// grid-crossing points along the SAME physical 3D edge disagree - one side
// gets an extra vertex partway along a segment the other side leaves
// whole, a classic T-junction, even though the edge's own true endpoints
// (and every original polyline sample) match exactly on both sides.
// Confirmed the same way on the fully-planar box+box case as on the
// curved box+cylinder/sphere+box ones, so this is not a curvature-specific
// gap; it is generic to any two independently-parameterized exact-clip
// faces meeting along this engine's own dense polyline boundary.
//
// FIX, entirely additive and confined to this file: tessellate exactly as
// Brep::Tessellate() already does (that function itself, and everything it
// calls in brep.cpp/surface.cpp, is untouched - BooleanCombineMixed's own
// tessellation is bit-for-bit unaffected), then, before welding, patch
// every face's own T-junctions by inserting the other side's extra
// boundary vertex into the coarser side's boundary edge - splitting the
// one triangle that owns that edge into a fan through the inserted
// point(s), preserving the original triangle's winding. Repeated to a
// fixed pass limit so a vertex inserted this pass can itself close a
// second, rarer chained mismatch next pass. Finally, degenerate
// (repeated-vertex, zero-area) triangles - a separate, pre-existing
// grid-clip artifact near a surface's own singular point (e.g. the exact
// pole of a trimmed sphere octant) that otherwise leaves spurious
// zero-length "edges" behind even after the T-junction pass - are dropped.
namespace {

struct MutFace {
  std::vector<Point3d> v;
  std::vector<std::array<int, 3>> f;
};

MutFace ToMutFace(const Mesh& m) {
  MutFace out;
  const ON_Mesh& raw = m.raw();
  out.v.reserve(static_cast<size_t>(raw.m_V.Count()));
  for (int i = 0; i < raw.m_V.Count(); ++i) {
    const ON_3fPoint& p = raw.m_V[i];
    out.v.emplace_back(static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z));
  }
  out.f.reserve(static_cast<size_t>(raw.m_F.Count()));
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& mf = raw.m_F[i];
    out.f.push_back({mf.vi[0], mf.vi[1], mf.vi[2]});
    if (mf.IsQuad()) out.f.push_back({mf.vi[0], mf.vi[2], mf.vi[3]});
  }
  return out;
}

Mesh FromMutFace(const MutFace& m) {
  Mesh out;
  ON_Mesh& raw = out.raw();
  for (const Point3d& p : m.v) {
    raw.m_V.Append(ON_3fPoint(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)));
  }
  for (const std::array<int, 3>& t : m.f) {
    ON_MeshFace mf;
    mf.vi[0] = t[0];
    mf.vi[1] = t[1];
    mf.vi[2] = t[2];
    mf.vi[3] = t[2];
    raw.m_F.Append(mf);
  }
  return out;
}

// Whether zero-area triangle `t` (a repeated-vertex or truly collinear
// degenerate produced by grid-clipping right at a surface's own singular
// point) should be dropped.
bool IsDegenerateTriangle(const MutFace& mf, const std::array<int, 3>& t) {
  if (t[0] == t[1] || t[1] == t[2] || t[2] == t[0]) return true;
  const Point3d& a = mf.v[static_cast<size_t>(t[0])];
  const Point3d& b = mf.v[static_cast<size_t>(t[1])];
  const Point3d& c = mf.v[static_cast<size_t>(t[2])];
  const Vector3d ab(b.x - a.x, b.y - a.y, b.z - a.z);
  const Vector3d ac(c.x - a.x, c.y - a.y, c.z - a.z);
  const Vector3d cross(ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x);
  const double area2 = cross.x * cross.x + cross.y * cross.y + cross.z * cross.z;
  return area2 < 1e-20;
}

// True, with `t_out` set, when `p` sits strictly between `a` and `b` on
// the segment they span (excluding the endpoints themselves - an
// already-shared vertex needs no patching).
bool PointStrictlyOnSegment(const Point3d& a, const Point3d& b, const Point3d& p, double tol, double& t_out) {
  const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
  const double len2 = abx * abx + aby * aby + abz * abz;
  const double len = std::sqrt(len2);
  if (len < tol) return false;
  // Tolerance scaled to THIS segment's own length: a curved face's own
  // grid-inserted point on what is (for this engine's own straight-edge
  // faces) a genuinely straight 3D line can deviate from that line by a
  // small "bulge" proportional to the segment's own length (the surface's
  // curved (u, v) chart evaluated along a straight-in-UV line isn't
  // straight in 3D - see this function's own caller's doc comment), not
  // a fixed absolute amount - so the perpendicular-distance check below
  // scales with segment length while the along-segment check stays
  // absolute (`tol`), keeping this from ever matching an unrelated,
  // merely-nearby point on a long segment.
  const double perp_tol = std::max(tol, len * 5e-3);
  const double apx = p.x - a.x, apy = p.y - a.y, apz = p.z - a.z;
  const double t = (apx * abx + apy * aby + apz * abz) / len2;
  const double eps_t = tol / len;
  if (t <= eps_t || t >= 1.0 - eps_t) return false;
  const double px = a.x + abx * t, py = a.y + aby * t, pz = a.z + abz * t;
  const double dx = p.x - px, dy = p.y - py, dz = p.z - pz;
  if (dx * dx + dy * dy + dz * dz > perp_tol * perp_tol) return false;
  t_out = t;
  return true;
}

// One stitching pass over every face's own boundary edges. Returns the
// number of edges patched (0 => converged, nothing left to do).
int StitchTJunctionsOnce(std::vector<MutFace>& faces, double tol) {
  int patched = 0;
  for (size_t fi = 0; fi < faces.size(); ++fi) {
    MutFace& mf = faces[fi];
    // This face's own boundary edges: a directed edge whose reverse
    // doesn't also appear among this SAME face's own triangles.
    std::map<std::pair<int, int>, int> directed_owner;  // (a,b) -> triangle index
    for (size_t ti = 0; ti < mf.f.size(); ++ti) {
      const std::array<int, 3>& t = mf.f[ti];
      directed_owner[{t[0], t[1]}] = static_cast<int>(ti);
      directed_owner[{t[1], t[2]}] = static_cast<int>(ti);
      directed_owner[{t[2], t[0]}] = static_cast<int>(ti);
    }
    std::vector<std::pair<std::pair<int, int>, int>> boundary;  // ((a,b), tri)
    for (const auto& [edge, tri] : directed_owner) {
      if (directed_owner.count({edge.second, edge.first}) == 0) {
        boundary.emplace_back(edge, tri);
      }
    }
    if (boundary.empty()) continue;

    // Triangles this pass replaces, and the fans that replace them -
    // applied once, after scanning every boundary edge of this face, so
    // triangle indices found above stay valid throughout the scan.
    std::vector<bool> removed(mf.f.size(), false);
    std::vector<std::array<int, 3>> additions;

    for (const auto& [edge, tri_idx] : boundary) {
      if (removed[static_cast<size_t>(tri_idx)]) continue;  // already replaced this pass
      const int a_idx = edge.first, b_idx = edge.second;
      const Point3d& a = mf.v[static_cast<size_t>(a_idx)];
      const Point3d& b = mf.v[static_cast<size_t>(b_idx)];

      // Candidate extra points: every OTHER face's own vertex (a
      // T-junction is always introduced by a DIFFERENT face's denser
      // sampling of this same shared physical edge - this face's own
      // interior vertices are never candidates for its own boundary).
      std::vector<std::pair<double, Point3d>> hits;  // (t along a->b, point)
      for (size_t gi = 0; gi < faces.size(); ++gi) {
        if (gi == fi) continue;
        for (const Point3d& p : faces[gi].v) {
          double t;
          if (PointStrictlyOnSegment(a, b, p, tol, t)) hits.emplace_back(t, p);
        }
      }
      if (hits.empty()) continue;
      std::sort(hits.begin(), hits.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
      // De-dup near-identical t values (the same physical point found via
      // more than one other face, or two other faces sharing that exact
      // vertex themselves).
      std::vector<Point3d> chain;
      chain.push_back(a);
      for (const auto& [t, p] : hits) {
        if (!chain.empty()) {
          const Point3d& last = chain.back();
          const double dx = p.x - last.x, dy = p.y - last.y, dz = p.z - last.z;
          if (dx * dx + dy * dy + dz * dz < tol * tol) continue;
        }
        chain.push_back(p);
      }
      chain.push_back(b);
      if (chain.size() <= 2) continue;  // nothing new after de-dup

      // Find the third ("apex") vertex of the owning triangle, and its
      // exact winding, so the fan preserves the original orientation.
      const std::array<int, 3>& t = mf.f[static_cast<size_t>(tri_idx)];
      int apex_idx = -1;
      for (int k = 0; k < 3; ++k) {
        if (t[static_cast<size_t>(k)] != a_idx && t[static_cast<size_t>(k)] != b_idx) {
          apex_idx = t[static_cast<size_t>(k)];
          break;
        }
      }
      if (apex_idx < 0) continue;  // shouldn't happen for a real triangle

      std::vector<int> chain_idx;
      chain_idx.push_back(a_idx);
      for (size_t k = 1; k + 1 < chain.size(); ++k) {
        chain_idx.push_back(static_cast<int>(mf.v.size()));
        mf.v.push_back(chain[k]);
      }
      chain_idx.push_back(b_idx);

      for (size_t k = 0; k + 1 < chain_idx.size(); ++k) {
        additions.push_back({apex_idx, chain_idx[k], chain_idx[k + 1]});
      }
      removed[static_cast<size_t>(tri_idx)] = true;
      ++patched;
    }

    if (patched > 0) {
      std::vector<std::array<int, 3>> next;
      next.reserve(mf.f.size() + additions.size());
      for (size_t ti = 0; ti < mf.f.size(); ++ti) {
        if (!removed[ti]) next.push_back(mf.f[ti]);
      }
      for (const std::array<int, 3>& t : additions) next.push_back(t);
      mf.f = std::move(next);
    }
  }
  return patched;
}

}  // namespace

// --- TessellateGeneralBooleanClosedMesh(): edge-topology-conforming ----
// reconciliation (real curved-edge fix; StitchTJunctionsOnce() above is
// kept, unmodified, as a fallback safety net - see its own effect on
// box+box).
//
// ROOT CAUSE, confirmed directly on box+cylinder Union (sweep case 01):
// every interior cut edge this engine builds (BuildLoop(), above) is a
// genuine, single shared ON_BrepEdge whose own 3D curve is a STRAIGHT
// ON_LineCurve between two welded ON_BrepVertex points, and BOTH adjacent
// faces' own 2D trims for that edge are ALSO straight lines, in THEIR OWN
// (u, v) space (BuildLoop() constructs each trim's c2 the same way as the
// edge's own c3: one ON_LineCurve per polyline segment). For a PLANAR
// face that is an affine map, so a straight-(u, v) trim IS a straight-3D
// trim: NurbsSurface::TessellateGridClippedExact()'s own grid-crossing
// insertions along it land exactly on the edge's own straight 3D chord -
// exactly what StitchTJunctionsOnce()'s own perp-tolerance check above
// expects. For a CURVED face (a cylinder wall's own angular direction,
// say), the SAME straight-(u, v) trim maps through that face's OWN
// curved surface to a genuinely CURVED 3D path (the wall's own true
// circular arc between the two shared endpoints) - not the chord at all.
// So the cylinder side's own grid-inserted points sit on the TRUE circle,
// bowed away from the chord by an amount that scales with the arc's OWN
// curvature (sagitta), not ordinary Newton/round-off noise - confirmed
// directly to exceed StitchTJunctionsOnce()'s own perp_tol on this
// fixture at the sweep's own division count, which is why that pass
// alone leaves the entire circle's worth of boundary edges unmatched
// (1327 of them, not a handful of stray T-junctions - see
// boolean_general.h's own doc comment).
//
// FIX: for every INTERIOR edge (exactly two trims) of `result`'s own real
// topology, walk each of its two adjacent faces' own raw tessellation
// boundary from that edge's start vertex to its end vertex (a short walk:
// this engine gives every original polyline segment ITS OWN ON_BrepEdge -
// BuildLoop() again - so the run between two immediately-consecutive edge
// endpoints is at most the handful of points either face's OWN grid
// happened to insert along that one short span), collect the UNION of
// both sides' own insertion points as fractions `t` along the edge's own
// straight chord (not their raw, possibly curve-bowed 3D position), and
// rebuild BOTH sides' boundary triangulation from the SAME merged,
// chord-interpolated point at each shared `t` - so a merged point is
// float-for-float (Mesh itself stores ON_3fPoint) IDENTICAL on both
// sides, not merely "close enough" to clear a tolerance. This is a small,
// bounded, already-disclosed approximation (boolean_general.h: "the
// assembled B-rep's edges are polygonal approximations of the true
// intersection curves"): a curved face's own newly-snapped boundary point
// moves from the true surface onto the edge's own straight chord, no
// worse than the chord error the ORIGINAL, coarser edge already carried
// before this function ever ran.
//
// Falls back silently (leaves an edge for StitchTJunctionsOnce() above to
// try) whenever the walk can't find a clean run between the two
// endpoints on one side - a self-seam edge (both trims on the SAME face,
// e.g. a cylindrical wall's own vertical seam) in particular is left to
// that pass, which already handles it (that seam is straight in 3D too,
// so the plain perp-tolerance match already closes it - confirmed: this
// exclusion does not regress box+box or any other case that was already
// closing).
namespace {

// a -> (b, owning triangle index), for every directed boundary edge (a,
// b) of `mf` whose reverse (b, a) is NOT also one of `mf`'s own directed
// triangle edges.
struct BoundaryGraph {
  std::unordered_map<int, std::pair<int, int>> next;
};

BoundaryGraph BuildBoundaryGraph(const MutFace& mf) {
  std::map<std::pair<int, int>, int> directed_owner;
  for (size_t ti = 0; ti < mf.f.size(); ++ti) {
    const std::array<int, 3>& t = mf.f[ti];
    directed_owner[{t[0], t[1]}] = static_cast<int>(ti);
    directed_owner[{t[1], t[2]}] = static_cast<int>(ti);
    directed_owner[{t[2], t[0]}] = static_cast<int>(ti);
  }
  BoundaryGraph g;
  // A vertex with MORE THAN ONE outgoing boundary edge (a pinch point -
  // e.g. where a dropped degenerate sliver used to join two otherwise-
  // separate boundary runs, see this function's own doc comment) has no
  // single well-defined "next" hop; `ambiguous` marks it so the walk
  // below treats it as a dead end rather than silently picking whichever
  // candidate this map happened to see first (confirmed directly to send
  // the walk on a long, wrong detour otherwise - see ReconcileEdgeTopology's
  // own doc comment).
  std::unordered_set<int> ambiguous;
  for (const auto& [edge, tri] : directed_owner) {
    if (directed_owner.count({edge.second, edge.first}) == 0) {
      if (!g.next.emplace(edge.first, std::make_pair(edge.second, tri)).second) ambiguous.insert(edge.first);
    }
  }
  for (int a : ambiguous) g.next.erase(a);
  return g;
}

// The boundary-graph vertex (an edge-starting vertex) nearest `p`, within
// `tol` - or -1 if none is that close.
int NearestBoundaryStart(const MutFace& mf, const BoundaryGraph& g, const Point3d& p, double tol) {
  int best = -1;
  double best_d2 = tol * tol;
  for (const auto& [a, unused] : g.next) {
    (void)unused;
    const Point3d& v = mf.v[static_cast<size_t>(a)];
    const double dx = v.x - p.x, dy = v.y - p.y, dz = v.z - p.z;
    const double d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < best_d2) {
      best_d2 = d2;
      best = a;
    }
  }
  return best;
}

// One hop of a walked boundary chain: `idx` is the vertex, `tri` is the
// triangle owning the boundary edge FROM this vertex to the next one in
// the chain (unused - -1 - on the chain's own last entry).
struct WalkStep {
  int idx;
  int tri;
};

// Walks `g`'s own boundary graph from `start` until reaching a vertex
// within `tol` of `end_p`, appending each visited vertex (and the
// triangle owning its outgoing edge) to `out`. Returns false - `out` left
// in a partial, unusable state - if the walk dead-ends, runs past a small
// step cap, or (the `p0`/`p1`/`max_dev` check) ever visits a vertex too
// far from the edge's own straight chord to plausibly be one of ITS OWN
// grid-inserted points: this engine gives every original polyline
// segment its own ON_BrepEdge (see ReconcileEdgeTopology's own doc
// comment), so a genuine run between two consecutive such edges' own
// endpoints is always SHORT and stays close to their shared chord: a run
// that wanders far or long is walking a DIFFERENT part of this face's own
// boundary entirely (confirmed directly: without this check, a face with
// a pinch point left by BuildBoundaryGraph()'s own ambiguous-vertex
// pruning could still walk 100+ hops around most of its own loop before
// coincidentally landing back within `tol` of `end_p` - corrupting, not
// just failing, box+box's own previously-closing Intersection/A-B/B-A).
bool WalkBoundaryChain(const MutFace& mf, const BoundaryGraph& g, int start, const Point3d& end_p, double tol,
                        const Point3d& p0, const Point3d& p1, double max_dev, std::vector<WalkStep>& out) {
  out.clear();
  int cur = start;
  const double tol2 = tol * tol;
  constexpr int kMaxSteps = 48;
  for (int steps = 0; steps < kMaxSteps; ++steps) {
    const Point3d& cur_p = mf.v[static_cast<size_t>(cur)];
    const double dx = cur_p.x - end_p.x, dy = cur_p.y - end_p.y, dz = cur_p.z - end_p.z;
    if (dx * dx + dy * dy + dz * dz <= tol2) {
      out.push_back({cur, -1});
      return true;
    }
    const double t = ProjectT(p0, p1, cur_p);
    if (t < -0.5 || t > 1.5) return false;  // well outside this edge's own span
    const Point3d on_chord = ChordPoint(p0, p1, t);
    const double ddx = cur_p.x - on_chord.x, ddy = cur_p.y - on_chord.y, ddz = cur_p.z - on_chord.z;
    if (ddx * ddx + ddy * ddy + ddz * ddz > max_dev * max_dev) return false;  // too far off-chord
    const auto it = g.next.find(cur);
    if (it == g.next.end()) return false;
    out.push_back({cur, it->second.second});
    cur = it->second.first;
  }
  return false;
}

// Reconciles one face's own already-walked boundary run (`chain`, from
// `p0` to `p1`) to contain exactly `merged_t`'s points: every EXISTING
// chain vertex is snapped onto the chord at its own `t` (so it agrees,
// float-for-float, with the other side's copy of the same `t` - both
// sides compute it with the same ChordPoint() formula), and every
// `merged_t` value missing from this side is fanned into the triangle
// that currently owns the span containing it. Mutates `mf` in place.
void ReconcileChainToChord(MutFace& mf, const std::vector<WalkStep>& chain, const Point3d& p0, const Point3d& p1,
                            const std::vector<double>& merged_t, double tol, double edge_len) {
  const size_t n = chain.size();
  if (n < 2) return;

  std::vector<double> t_here(n);
  for (size_t k = 0; k < n; ++k) t_here[k] = ProjectT(p0, p1, mf.v[static_cast<size_t>(chain[k].idx)]);
  // Snap every existing chain vertex onto the chord at its own `t`.
  for (size_t k = 0; k < n; ++k) mf.v[static_cast<size_t>(chain[k].idx)] = ChordPoint(p0, p1, t_here[k]);

  const double eps_t = edge_len > 1e-12 ? tol / edge_len : 1e-9;
  std::vector<bool> removed(mf.f.size(), false);
  std::vector<std::array<int, 3>> additions;

  for (size_t k = 0; k + 1 < n; ++k) {
    const int a_idx = chain[k].idx, b_idx = chain[k + 1].idx;
    const int tri_idx = chain[k].tri;
    if (tri_idx < 0 || static_cast<size_t>(tri_idx) >= mf.f.size() || removed[static_cast<size_t>(tri_idx)]) continue;
    const double t_lo = t_here[k], t_hi = t_here[k + 1];
    const double t_min = std::min(t_lo, t_hi), t_max = std::max(t_lo, t_hi);
    const bool increasing = t_hi >= t_lo;

    std::vector<double> extra;
    for (double t : merged_t) {
      if (t > t_min + eps_t && t < t_max - eps_t) extra.push_back(t);
    }
    if (extra.empty()) continue;
    std::sort(extra.begin(), extra.end());
    if (!increasing) std::reverse(extra.begin(), extra.end());

    const std::array<int, 3>& tri = mf.f[static_cast<size_t>(tri_idx)];
    int apex_idx = -1;
    for (int kk = 0; kk < 3; ++kk) {
      if (tri[static_cast<size_t>(kk)] != a_idx && tri[static_cast<size_t>(kk)] != b_idx) {
        apex_idx = tri[static_cast<size_t>(kk)];
        break;
      }
    }
    if (apex_idx < 0) continue;

    std::vector<int> chain_idx;
    chain_idx.push_back(a_idx);
    for (double t : extra) {
      const Point3d p = ChordPoint(p0, p1, t);
      const Point3d& last = mf.v[static_cast<size_t>(chain_idx.back())];
      const double dx = p.x - last.x, dy = p.y - last.y, dz = p.z - last.z;
      if (dx * dx + dy * dy + dz * dz < tol * tol) continue;  // de-dup a near-zero-length sub-segment
      chain_idx.push_back(static_cast<int>(mf.v.size()));
      mf.v.push_back(p);
    }
    chain_idx.push_back(b_idx);
    if (chain_idx.size() <= 2) continue;

    for (size_t kk = 0; kk + 1 < chain_idx.size(); ++kk) additions.push_back({apex_idx, chain_idx[kk], chain_idx[kk + 1]});
    removed[static_cast<size_t>(tri_idx)] = true;
  }

  if (!additions.empty()) {
    std::vector<std::array<int, 3>> next;
    next.reserve(mf.f.size() + additions.size());
    for (size_t ti = 0; ti < mf.f.size(); ++ti) {
      if (!removed[ti]) next.push_back(mf.f[ti]);
    }
    for (const std::array<int, 3>& t : additions) next.push_back(t);
    mf.f = std::move(next);
  }
}

// Drives the whole pass: every interior (two-trim) edge of `brep`, whose
// two trims land on two DIFFERENT faces (a self-seam - same face on both
// trims - is left to StitchTJunctionsOnce(), see this section's own doc
// comment), gets its two adjacent faces' boundaries reconciled to a
// shared, chord-snapped point set. `faces[i]` is assumed to be
// `brep.m_F[i]`'s own tessellation (Brep::Tessellate()'s own documented
// face-index order) - silently skipped whenever that correspondence
// isn't exactly 1:1 (e.g. a face Brep::Tessellate() itself dropped),
// since there is then no reliable way to know which MutFace an edge's
// own face index means.
void ReconcileEdgeTopology(const ON_Brep& brep, std::vector<MutFace>& faces, double tol) {
  if (static_cast<size_t>(brep.m_F.Count()) != faces.size()) return;

  std::vector<bool> has_graph(faces.size(), false);
  std::vector<BoundaryGraph> graphs(faces.size());
  auto graph_for = [&](int fi) -> BoundaryGraph& {
    if (!has_graph[static_cast<size_t>(fi)]) {
      graphs[static_cast<size_t>(fi)] = BuildBoundaryGraph(faces[static_cast<size_t>(fi)]);
      has_graph[static_cast<size_t>(fi)] = true;
    }
    return graphs[static_cast<size_t>(fi)];
  };
  auto invalidate = [&](int fi) { has_graph[static_cast<size_t>(fi)] = false; };

  for (int ei = 0; ei < brep.m_E.Count(); ++ei) {
    const ON_BrepEdge& edge = brep.m_E[ei];
    if (edge.TrimCount() != 2) continue;
    const ON_BrepTrim* t0 = edge.Trim(0);
    const ON_BrepTrim* t1 = edge.Trim(1);
    if (!t0 || !t1) continue;
    const ON_BrepFace* face_a = t0->Face();
    const ON_BrepFace* face_b = t1->Face();
    if (!face_a || !face_b) continue;
    const int fa = face_a->m_face_index;
    const int fb = face_b->m_face_index;
    if (fa == fb) continue;  // self-seam: StitchTJunctionsOnce() handles it
    if (fa < 0 || fb < 0 || static_cast<size_t>(fa) >= faces.size() || static_cast<size_t>(fb) >= faces.size()) continue;

    const ON_3dPoint& raw_p0 = brep.m_V[edge.m_vi[0]].point;
    const ON_3dPoint& raw_p1 = brep.m_V[edge.m_vi[1]].point;
    const Point3d p0(raw_p0.x, raw_p0.y, raw_p0.z);
    const Point3d p1(raw_p1.x, raw_p1.y, raw_p1.z);
    const double edge_len =
        std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y) + (p1.z - p0.z) * (p1.z - p0.z));
    if (edge_len < tol) continue;

    MutFace& mfa = faces[static_cast<size_t>(fa)];
    MutFace& mfb = faces[static_cast<size_t>(fb)];
    // Deliberately NOT scaled to edge_len: p0/p1 are exact, real
    // ON_BrepVertex positions that Brep::Tessellate()'s own boundary
    // reconstruction reproduces to float precision (Mesh stores
    // ON_3fPoint) on EVERY face that uses them - the same "every original
    // polyline sample matches exactly on both sides" guarantee
    // StitchTJunctionsOnce() above already relies on - so a small,
    // fixed-size tolerance is correct; a fraction of edge_len is not
    // (confirmed by direct measurement: it wrongly matched an unrelated
    // nearby vertex on a short segment, corrupting even the box+box case).
    const double walk_tol = std::max(tol, 1e-9) * 10.0;
    // How far off the chord a genuine grid-inserted point on THIS one
    // short span may plausibly sit (the curvature "bulge" this section's
    // own doc comment describes) - generous relative to the span itself,
    // but still bounded, so a walk that strays onto an unrelated part of
    // the face's own boundary (e.g. via a pinch point) is rejected long
    // before it could wander back and falsely land near the other
    // endpoint (see WalkBoundaryChain's own doc comment).
    const double max_dev = std::max(walk_tol, edge_len * 1.5);

    auto find_chain = [&](MutFace& mf, BoundaryGraph& g, const Point3d& from_p, const Point3d& to_p,
                           std::vector<WalkStep>& out) -> bool {
      const int s = NearestBoundaryStart(mf, g, from_p, walk_tol);
      if (s < 0) return false;
      return WalkBoundaryChain(mf, g, s, to_p, walk_tol, from_p, to_p, max_dev, out);
    };

    std::vector<WalkStep> chain_a, chain_b;
    bool ok_a = find_chain(mfa, graph_for(fa), p0, p1, chain_a);
    if (!ok_a) ok_a = find_chain(mfa, graph_for(fa), p1, p0, chain_a);
    bool ok_b = find_chain(mfb, graph_for(fb), p0, p1, chain_b);
    if (!ok_b) ok_b = find_chain(mfb, graph_for(fb), p1, p0, chain_b);
    if (!ok_a || !ok_b) continue;  // no clean run on one side - leave for the fallback pass

    std::vector<double> merged_t;
    for (size_t k = 1; k + 1 < chain_a.size(); ++k) merged_t.push_back(ProjectT(p0, p1, mfa.v[static_cast<size_t>(chain_a[k].idx)]));
    for (size_t k = 1; k + 1 < chain_b.size(); ++k) merged_t.push_back(ProjectT(p0, p1, mfb.v[static_cast<size_t>(chain_b[k].idx)]));
    if (merged_t.empty()) continue;  // both sides already a single direct span - nothing to reconcile
    std::sort(merged_t.begin(), merged_t.end());
    const double eps_t = edge_len > 1e-12 ? tol / edge_len : 1e-9;
    std::vector<double> deduped;
    for (double t : merged_t) {
      if (deduped.empty() || t - deduped.back() > eps_t) {
        deduped.push_back(t);
      } else {
        deduped.back() = 0.5 * (deduped.back() + t);
      }
    }

    if (std::getenv("DINO8_RECONCILE_DEBUG")) {
      std::fprintf(stderr, "edge %d: fa=%d fb=%d chainA=%zu chainB=%zu merged=%zu (raw %zu) edge_len=%g\n", ei, fa, fb,
                   chain_a.size(), chain_b.size(), deduped.size(), merged_t.size(), edge_len);
    }
    ReconcileChainToChord(mfa, chain_a, p0, p1, deduped, tol, edge_len);
    ReconcileChainToChord(mfb, chain_b, p0, p1, deduped, tol, edge_len);
    invalidate(fa);
    invalidate(fb);
  }
}

}  // namespace

Mesh TessellateGeneralBooleanClosedMesh(const Brep& result, int u_divisions, int v_divisions) {
  const std::vector<Mesh> raw_faces = result.Tessellate(u_divisions, v_divisions);
  std::vector<MutFace> faces;
  faces.reserve(raw_faces.size());
  for (const Mesh& m : raw_faces) faces.push_back(ToMutFace(m));

  // Tolerance scaled to the model's own extent, same spirit as
  // Mesh::MergeAndWeld()'s own default - fine enough to never merge two
  // genuinely distinct points, coarse enough to catch the same physical
  // point reconstructed independently by two different grids/surfaces.
  double diag = 0.0;
  for (const MutFace& mf : faces) {
    for (const Point3d& p : mf.v) {
      diag = std::max(diag, std::fabs(p.x));
      diag = std::max(diag, std::fabs(p.y));
      diag = std::max(diag, std::fabs(p.z));
    }
  }
  const double tol = std::max(1e-6, diag * 1e-6);

  // Drop each face's own degenerate (zero-area) triangles in place. These
  // are pre-existing grid-clip artifacts, already present in
  // `result.Tessellate()`'s own raw per-face output (confirmed by direct
  // inspection, before this function's stitcher ever runs) - typically a
  // spurious zero-width sliver sitting exactly on top of what is
  // otherwise a face's genuine boundary edge (three boundary-line points,
  // no off-line apex).
  auto drop_degenerate_triangles = [](std::vector<MutFace>& fs) {
    for (MutFace& mf : fs) {
      std::vector<std::array<int, 3>> kept;
      kept.reserve(mf.f.size());
      for (const std::array<int, 3>& t : mf.f) {
        if (!IsDegenerateTriangle(mf, t)) kept.push_back(t);
      }
      mf.f = std::move(kept);
    }
  };

  // Do this BEFORE T-junction stitching, not after: dropping a sliver
  // that sits on a face's own boundary can strand that sliver's two
  // OTHER edges - which the sliver had "claimed" as its own, so
  // StitchTJunctionsOnce never treated them as needing a cross-face
  // partner - as fresh, unmatched boundary edges once the sliver is
  // gone. Dropping first instead exposes the face's TRUE boundary (with
  // the spurious sliver already gone) before the stitcher ever runs, so
  // it treats those edges like any other boundary edge and fills them
  // from the neighboring face's own matching points. Confirmed by direct
  // measurement: box+box Intersection/A-B (32x32 and 32x128 divisions)
  // had 9 and 27 such leftover boundary edges respectively, every one of
  // them at exactly this pattern (a dropped sliver's ex-neighbor edge),
  // and both drop to 0 with this reordering alone.
  drop_degenerate_triangles(faces);

  // Real curved-edge fix (see ReconcileEdgeTopology()'s own doc comment
  // just above): walks `result`'s own genuine ON_Brep edge topology and
  // reconciles each interior edge's two adjacent faces to a shared,
  // chord-snapped boundary point set, BEFORE the plain point-matching
  // StitchTJunctionsOnce() pass below - which still runs afterward,
  // unmodified, as a fallback for whatever this pass leaves untouched
  // (self-seam edges, and anything else it couldn't cleanly walk).
  ReconcileEdgeTopology(result.raw(), faces, tol);

  // Same reordering reasoning as the drop_degenerate_triangles() call
  // above, applied to a fresh source: ReconcileChainToChord()'s own
  // chord-snapping can occasionally leave a fan triangle degenerate (two
  // merged `t` values close enough to produce a near-zero-length
  // sub-segment survives its own de-dup but still ends up numerically
  // collinear with its neighbor once snapped) - confirmed directly on
  // box+box Union: 5 corner slivers of exactly this shape, each
  // stranding its own two other edges as fresh unmatched boundary once
  // dropped, UNLESS dropped here, before StitchTJunctionsOnce() ever
  // sees them (the very same stranding mechanism the first
  // drop_degenerate_triangles() call above was already written to
  // avoid).
  drop_degenerate_triangles(faces);

  for (int pass = 0; pass < 4; ++pass) {
    if (StitchTJunctionsOnce(faces, tol) == 0) break;
  }

  // A second, final pass: StitchTJunctionsOnce's own chain-insertion can
  // occasionally produce a fresh degenerate triangle of its own (three
  // chain points that end up numerically collinear), so sweep once more
  // after stitching converges. This second pass targets a DIFFERENT,
  // much rarer defect than the pre-stitch one above (confirmed separately
  // by direct measurement: it never fires on the box+box case, only on
  // curved-face cases where stitched chain points can be nearly
  // collinear along a curve's own local tangent).
  drop_degenerate_triangles(faces);

  std::vector<Mesh> patched;
  patched.reserve(faces.size());
  for (MutFace& mf : faces) patched.push_back(FromMutFace(mf));
  return Mesh::MergeAndWeld(patched, tol);
}

}  // namespace dino8::kernel
