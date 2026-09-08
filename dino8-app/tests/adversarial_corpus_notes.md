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
