# Adversarial geometry corpus: known kernel-level limitations

This file documents the specific cases in the adversarial test corpus
(`boolean_adversarial_script.txt`, `fillet_adversarial_script.txt`) that are
genuinely infeasible to fix within the geometry kernel this project has
(OpenNURBS + Manifold, no proprietary robustness layer). For each, the
command now fails gracefully (a clear warning, no crash/hang/silent-garbage
result) rather than faking success. Both limitations below were confirmed
reproducible on the unmodified, pre-existing code as well - they are not
regressions introduced while building this corpus, and no further attempt to
fix them is recommended within this kernel; the section headers say what
would actually be required.

## 3. Curve self-intersection corpus (`curve_adversarial_script.txt`) - fixed, not a limitation

Unlike sections 1-2 above, this one was a real, fixable bug rather than a
kernel-level limit: `PlanarSrf` and a solid-capping `Extrude` both called
`ON_BrepTrimmedPlane`, which builds a single trim loop unconditionally with
no check that the boundary curve is simple - fed a self-crossing closed
curve (a bowtie), it silently returned a `Brep` that topologically looks
like a closed solid (every edge has two trims) while its trim loop actually
crosses itself in 2D, a silent geometric corruption. Both commands now
reject a self-crossing boundary via a shared `CurveSelfIntersects` helper
(`src/commands/cmd_common.h`, a dense polyline-sampled segment/segment
test) before calling `ON_BrepTrimmedPlane`: `PlanarSrf` skips the curve
with a warning, `Extrude` falls through to the open ruled-surface path
(`SumSurface`, which has no "solid" claim to violate).

**Not attempted in this pass** (honestly out of scope, not fixed and not
proven infeasible - a future pass should pick these up): high-aspect-ratio
surfaces (one dimension orders of magnitude larger than the other) fed into
booleans/fillets/intersections, and self-intersecting sweep/loft rails or
cross-sections. Both were part of the original ask for this corpus
expansion but were not reached in the time available; this file's own
sections 1-2 above cover the *scale*-related robustness issues found so
far (huge-coordinate + small-feature precision, and radius-vs-tolerance),
which is adjacent but not the same as an extreme *aspect-ratio* case.

## 1. Single-precision mesh storage breaks at huge-coordinate + small-feature combinations

**Case:** `fillet_adversarial_script.txt`'s huge-scale section - a 10-unit
box translated to ~1,000,000-unit coordinates, filleted with a perfectly
ordinary radius of 2 (identical proportions to `fillet_script.txt`'s own
working 10x10x10-box case, which passes at the origin). Also reproducible
directly on `Volume`/`Area` for an even smaller (0.01-unit) box at a
~1,100,000-unit offset - see the git history of this file for the removed
`fillet_adversarial_script.txt` section that first found it (superseded by
the simpler, single-limitation case that's in the file now, to avoid one
test case entangling two different failure causes).

**Symptom before the fix landed:** `FilletEdge` printed
`"FilletEdge: edge 10 of object N replaced with an exact fillet (radius 2)"`
- a claimed SUCCESS - while `List` reported the result as a "closed solid"
(a purely topological check: every edge has two trims referencing it).
But `Volume` on that same object then failed with `"Object N is not
closed"` and reported `0`, and the `Check` command's real geometric
validity test (`ON_Brep::IsValid()`) reported `INVALID`. The fillet had a
real, silent gap in 3D space that the topological "closed" check could not
see.

**Root cause:** `dino8::kernel::Mesh` (used identically by rendering,
`BrepMesher`, `Remesh`, and every boolean/mesh operation in the app - not
something special to fillets) stores vertex coordinates in `ON_Mesh::m_V`,
which is `ON_3fPoint` - single-precision floats - regardless of how the
mesh was produced. At ~1e6-unit coordinate magnitude, a 32-bit float's
~7 significant decimal digits give an absolute quantization step of
roughly `1e6 * 2^-23 ~= 0.12` units. A 2-unit fillet radius (or a
0.01-unit box) is smaller than that quantization step, so *any* mesh this
pipeline produces at that coordinate scale - whether from tessellating an
exactly-computed NURBS B-rep, or from the mesh-based fallback booleans use
internally - loses the geometric detail before it is ever checked for
closure. This is not a tolerance-parameter problem: `BuildFillet`'s
scale-aware tolerance (added alongside this corpus) controls how
aggressively the *intersection/offset* math treats nearby features as
coincident, but it cannot recover precision the float vertex storage has
already discarded once a mesh exists. `dino8-kernel/src/boolean.cpp`'s
`FromManifold` carries the same note for booleans specifically; this is
the general form of that limitation, and it affects every mesh-based
operation in the kernel, not just `BooleanCombine`.

**What would actually fix it:** migrating `dino8::kernel::Mesh`'s vertex
storage off `ON_Mesh`'s single-precision array to a double-precision store
used consistently by every consumer (rendering, `BrepMesher`, `Remesh`,
booleans, fillets, ...) - a large, invasive, cross-cutting change well
outside the scope of a boolean/fillet robustness pass, and arguably outside
what OpenNURBS's own `ON_Mesh` type is designed to do at all (Rhino itself
recommends keeping model geometry within a few orders of magnitude of the
origin for exactly this class of reason).

**What was actually fixed here:** `FilletEdgeCommand::Run` (in
`dino8-app/src/commands/cmd_fillet.cpp`) no longer trusts the topological
"every edge has two trims" check as proof of a sound result. It now
additionally tessellates the candidate exact-trim brep and requires
`IsClosedManifold()` (the same watertightness test `Volume` already
relies on) before accepting it as "exact"; if that fails, it falls through
to the existing mesh-based fallback, and if the mesh fallback *also* comes
back non-watertight (as happens at this coordinate scale), the command
now fails with a clear message -
`"FilletEdge: could not build a watertight result at this object's
coordinate scale (both the exact B-rep trim and the mesh fallback came
back with a gap - see adversarial_corpus_notes.md)"` - and leaves the
original object untouched, instead of silently adding a broken result to
the document. (`ON_Brep::IsValid()` was tried as the validity check
instead of a tessellation-based one and rejected - it flagged even
`fillet_script.txt`'s existing, correct box-corner fillet as invalid; see
the comment beside `TrimPlanarFace` in the same file about
`ON_Brep::SetEdgeTolerance`'s recompute leaving edge tolerances at
`ON_UNSET_VALUE`. A tessellate-and-check-closure test was used instead
because it matches what `Volume`/`Check` actually measure, without that
false-positive.)

## 2. Fillet radius comparable to the document's absolute tolerance setting

**Case:** attempting `FilletEdge`/`FilletSrf` with a radius close to (or
smaller than) `ctx.Settings().absolute_tolerance` (default `0.001`) on
geometry whose own scale is comparably small (e.g. a box with 0.01-unit
edges and a requested radius of 0.001 - 10% of the edge length, which
would be a perfectly reasonable proportion at "normal" scale).

**Symptom:** `BuildFillet` reports `"the offset surfaces do not meet"` and
declines to build a fillet, even though the requested radius is a sane
fraction of the object's own size.

**Root cause:** `BuildFillet`'s effective tolerance
(`dino8-app/src/commands/cmd_fillet.cpp`) is
`max(document_absolute_tolerance, surface_scale * 1e-6)`. The
scale-relative term correctly floors the tolerance for large or small
surfaces relative to a *reasonable* document tolerance, but it cannot
override a document tolerance that is already coarser than the geometry
being asked for - and it should not, since `absolute_tolerance` is exactly
the setting Rhino-style CAD tools use to say "do not resolve detail finer
than this" throughout the whole document, not just for one command. If a
user's document tolerance is left at a default tuned for meter/inch-scale
models while their actual model is sub-millimeter, no algorithm can
distinguish "a feature the user wants resolved" from "noise the user asked
to have ignored" - this is a real, well-known modeling discipline in
Rhino itself (a document's tolerance is expected to match its geometry's
scale), not a gap specific to this fillet implementation.

**What would actually fix it:** nothing at the algorithm level - the
correct fix is a document/UI-level one (prompting the user to tighten
`absolute_tolerance` when their model's own bounding-box scale is much
smaller than the current setting implies), which is out of scope for a
boolean/fillet kernel-robustness pass.

**What was actually verified here:** the corpus confirms `BuildFillet`
fails cleanly with its existing diagnostic in this situation rather than
silently building a fillet using the wrong tolerance and shipping a subtly
wrong result - the graceful-failure contract already holds; no change was
needed once the scale-relative floor (added alongside this corpus) was in
place for the *opposite* problem (surfaces far larger than the document
tolerance, where the fixed 1e-4/1e-5 floors used to be needlessly tight).

## 3. FilletEdge on a closed (periodic) edge - fixed the loft/spine gap, found a deeper pre-existing mesh-fallback gap

**Case:** `FilletEdge` on a solid cylinder's own flat-top rim (the edge
between the top cap and the cylindrical wall, a full 360-degree closed
loop) - e.g. `Cylinder 0,0,0 5 20` then `FilletEdge Radius=1` picked on
that rim. Not exercised by any existing test: every prior `FilletEdge`/
`ChamferEdge` test corpus case (this file included) uses a box corner,
whose adjacent faces are both planar and get the *exact* B-rep trim path
- the mesh fallback investigated here has never actually been exercised
end-to-end by a passing test before.

**Symptom:** `"FilletEdge: could not build a watertight result at this
object's coordinate scale (both the exact B-rep trim and the mesh
fallback came back with a gap - see adversarial_corpus_notes.md)"`, at
ordinary (millimeter-scale, radius 1) coordinates - not the huge-scale
case documented above.

**Two real, distinct root causes were found and one was fixed:**

1. **Fixed**: `SweepTubeCutter`'s mesh-fallback cutter tube and
   `BuildFillet`'s own lofted fillet surface both treated the sampled
   spine as an open chain even when the underlying edge is closed.
   `LoftClosedRings()` (used for the cutter) caps *both* ends flat, which
   for a periodic spine leaves two coincident flat caps sitting on top of
   each other at the seam instead of a manifold join; separately,
   `BuildFillet`'s own `rows` array (fed into `LoftRows` to build the
   fillet surface) kept the raw first/last spine samples from
   `IntersectSurfaces`' marching tracer, which does not itself detect loop
   closure and left them a full ring-spacing or more apart (confirmed:
   ~0.3 units on a 5-unit-radius cylinder, roughly 1% of the loop's own
   circumference - not floating-point noise). Fixed via a new
   `Mesh::LoftPeriodicRings()` kernel primitive (bands wrap the last ring
   back to the first, no end caps - verified against `Mesh::Torus()`'s own
   reference volume for the identical parameterization, and against the
   exact analytic torus formula, in `dino8-kernel/tests/test_basic.cpp`)
   used by `SweepTubeCutter` whenever `ON_BrepEdge::IsClosed()` is true,
   plus snapping `BuildFillet`'s last row to an exact copy of the first
   whenever the chosen spine's own `IntersectionCurve::closed` flag is
   set. Both are real, independently verified fixes, not workarounds.
2. **Still open, pre-existing, and NOT specific to periodic edges**: even
   after both of the above, the cylinder-rim case above still fails.
   Diagnosis: `SweepTubeCutter`'s cutter tube was originally an
   independently-swept, deliberately oversized (`radius * 1.05`) circular
   tube unrelated to the fillet's own geometry, so the boolean difference
   gouged past the fillet's own analytic edges with nothing to re-trim the
   resulting cavity back down to them - a debug instrumentation pass
   measured `MergeAndWeld` collapsing only ~10 of the mesh pair's combined
   ~10,600 vertices, essentially no welding at all. Rebuilt
   `SweepTubeCutter` to cut a *wedge* instead (triangular cross-section
   `(spine, contact_a, contact_b)` at each sample, using the exact same
   contact points the fillet surface itself was lofted from, not an
   independently-chosen radius) - a real, verified improvement: the
   cutter's own volume is now sane and correctly signed (checked directly:
   the ring winding `(spine, contact_a, contact_b)` gives a positive
   volume; the reversed order gives -14.35 on the same test case, a wrong-
   but-still-"closed"-manifold result that `IsClosedManifold()` alone
   cannot catch - only checking the actual volume did), and welding
   improved roughly 20x (from ~10 to ~270 of ~8,700 vertices). Still not
   enough to close the mesh, and widening the weld tolerance to several
   times the fillet's own mesh chord tolerance barely moved that number
   (270 to ~370) - ruling out "just a tolerance problem." Root cause:
   `remainder_mesh`'s cut boundary (Manifold's own re-triangulation of the
   cutter/object intersection) and `fillet_mesh`'s boundary
   (`TessellateGridAdaptive`'s independent sampling of the same analytic
   surface) both approximate `contact_curve_a`/`contact_curve_b`, but as
   two separately-generated triangulations they don't share vertices
   pointwise even when their underlying curves are identical - this is a
   real, deeper architectural mismatch (non-conforming meshes at a shared
   boundary), not something a larger weld tolerance can safely paper over
   without risking incorrect merges elsewhere in the mesh.

**What would actually fix the remaining gap:** the two triangulations
need to be made *conforming* at their shared boundary - either by
constraining `TessellateGridAdaptive`'s boundary row to the exact same
sample points the cutter's own contact curves use (so Manifold's boolean
re-triangulation and the fillet's own tessellation start from an
identical polyline), or by inserting a proper constrained/conforming
remesh pass after the boolean. Both are a materially larger rewrite than
either fix already landed here, and honestly out of scope for this pass.
Filed here rather than silently left unmentioned, per this file's own
standard - and the wedge-cutter rewrite is kept regardless, since it is a
real, independently verified improvement (correct cavity volume/shape)
over the old oversized-tube approach even though it alone does not close
the remaining gap.
