# dino8-kernel — Chunk 1: Geometry Kernel Wrapper

Scope: Phase 0 of the Dino 8 blueprint. Wraps McNeel's open-source
[OpenNURBS](https://github.com/mcneel/opennurbs) toolkit with a smaller,
Dino8-facing API for NURBS curves/surfaces, B-reps, degree elevation, and
`.3dm` file I/O. This is the foundation every later chunk (booleans, SubD,
viewport, command engine) builds on.

## Exit criteria (from the blueprint's Phase 0 row)

- [x] Project builds and links against OpenNURBS
- [x] Can construct a NURBS curve and degree-elevate it
- [x] Can construct a NURBS surface and degree-elevate it
- [x] Can open and save a `.3dm` file (round-trip)
- [x] Can tessellate a surface/brep into a triangle mesh (own tessellator —
      see "Corrected assumptions" below)
- [x] Can perform a real boolean operation (union/intersection/difference)
      on two closed meshes, backed by Manifold
- [ ] Curve/surface edit operations beyond construction (left for a later
      kernel chunk)

## Corrected assumptions (read this before planning chunk 3+)

The original blueprint assumed booleans and meshing could be built by
"wrapping OpenNURBS." Verified directly against the v8.34 source — not
assumed — that's wrong on both counts:

- **No boolean operations exist in OpenNURBS at all.** There is no
  `BooleanUnion`/`BooleanIntersection`/`BooleanDifference` anywhere in the
  public API, for B-reps or meshes. Rhino's actual boolean engine is
  closed-source and lives outside OpenNURBS entirely.
- **`ON_Brep::CreateMesh` and `ON_Surface::CreateMesh` are declared but
  have no implementation in the public source.** They're stubs for
  Rhino's closed-source mesher. Calling them links successfully against
  the header but fails at link time with an undefined reference — this
  isn't a hidden edge case, it's the very first thing chunk 2 hit.

What this repo does instead:

- `NurbsSurface::TessellateGrid()` / `Brep::Tessellate()` are a
  **from-scratch grid tessellator** we own (uniform UV sampling +
  triangulation), not OpenNURBS'. It only handles untrimmed surfaces and
  isn't adaptive/curvature-aware — good enough to feed the boolean
  engine, not a real product's mesher.
- `BooleanCombine()` (chunk 2, `include/dino8/kernel/boolean.h`) is the
  real boolean engine, backed by
  [Manifold](https://github.com/elalish/manifold) (fetched via CMake,
  pinned to v3.5.2) rather than anything from OpenNURBS. It's exercised
  end-to-end in `tests/test_basic.cpp`: two overlapping unit cubes,
  union/intersection/difference, each checked against the exact expected
  volume (8 + 8 − 1, 1, and 8 − 1 respectively) via a from-scratch
  divergence-theorem volume calculation (`Mesh::Volume()`), not just
  "did it not crash."
- Manifold requires genuinely closed/watertight input; `BooleanCombine()`
  surfaces that as a thrown `std::runtime_error` (via `Manifold::Status()`)
  rather than silently producing garbage geometry.
- `Brep::Box()` + `Mesh::MergeAndWeld()` close the gap this section used to
  describe as open: `Brep::Tessellate()` tessellates each face
  independently, so two faces meeting at a shared edge each produce their
  own copy of that edge's vertices — coincident positions, but separate
  array entries, which is not a closed manifold as far as Manifold is
  concerned. `MergeAndWeld()` snaps near-coincident vertices together and
  remaps face indices, turning "six independently-tessellated open
  patches that happen to line up" into one genuinely closed mesh.
  `Brep::TessellateToClosedMesh()` does both steps in one call. Verified
  end-to-end in `tests/test_basic.cpp`: `Brep::Box()` → tessellate → weld
  → `BooleanCombine()`, checked against the same exact volumes as the
  hand-built-mesh boolean tests.
- `Brep::Sphere()` closes the curved-surface gap the previous section left
  open: `Box()`'s six faces are flat, so it never exercised whether
  `MergeAndWeld()` actually handles independently-tessellated *curved*
  geometry, or a face welding against *its own* seam/poles rather than a
  neighboring face. `Sphere()` builds one curved face via OpenNURBS' own
  exact rational-NURBS sphere conversion (`ON_Sphere::GetNurbForm` — real
  math we didn't have to derive), tessellates it, and welds its u-seam
  (u=0 and u=2π are the same meridian) and both poles (every u value at
  the top/bottom row collapses to one physical point) shut against
  themselves. Verified in `tests/test_basic.cpp`: welding measurably
  reduces the raw grid's vertex count, the resulting mesh's volume is
  within 1% of the exact `4/3·π·r³`, and a real sphere-sphere
  `BooleanCombine()` intersection matches the closed-form two-equal-sphere
  lens-volume formula within 3% — checked against real geometry, not just
  "didn't crash."
- `Brep::TrimmedPlanarFace()` closes the "no trimmed surfaces" gap the
  previous section called out: it's real (if simplified) B-rep trimming —
  a face's surface plus a closed polygon in that surface's own (u, v)
  parameter space — rather than storing genuine ON_Brep loop/trim/edge
  topology (vertices, edges, paired 2D/3D curves), a much larger API
  surface. `NurbsSurface::TessellateGrid()` was extended to take that
  polygon and only emit grid cells whose four corners fall inside it,
  dropping (not clipping) boundary cells — the trimmed edge is exactly as
  precise as the grid resolution, not curve-fit. Verified in
  `tests/test_basic.cpp` with a case designed to have zero floating-point
  ambiguity: a 10×10 planar face trimmed to an inner square whose boundary
  deliberately doesn't land on any grid line, checked against
  hand-derived (not measured-after-the-fact) exact numbers — 49 vertices,
  72 triangles, and a trimmed area of exactly 36 — all of which matched.

## What's still not done (as of chunk 2)

- `Brep::Box()`, `Brep::Sphere()`, and `Brep::TrimmedPlanarFace()` are
  three hard-coded shapes, not general solid construction — no primitive
  library beyond these three, and no way to turn a trimmed face into part
  of a *closed* solid yet (`TrimmedPlanarFace()` only proves the
  tessellation-level trim; nothing here builds the matching edges/walls a
  real trimmed solid — e.g. an actual cylinder with disk caps — would
  need, so a trimmed face can't feed `BooleanCombine()` yet the way
  `Box()`/`Sphere()` can). They exist to prove the Brep → Tessellate →
  weld → boolean pipeline piece by piece, not to be a real modeler.
- The trim-polygon test in `TessellateGrid()` is whole-cell in/out
  (a cell is kept only if all four corners are inside), not real boundary
  clipping — a curved or diagonal trim edge will look faceted/staircased
  at low resolution, not smooth.
- `Mesh::MergeAndWeld()`'s tolerance-based vertex snapping is validated
  now on flat (Box) and curved (Sphere) closed primitives, but still only
  ones built from a single Brep's own faces meeting at exactly shared
  control points/parameterizations — not yet validated against two
  *independently constructed* surfaces whose shared boundary curves are
  merely geometrically coincident rather than parametrically identical.
- SubD modeling, adaptive/curvature-aware meshing, the viewport/display
  engine, GPU path tracer, command engine, UI shell, visual scripting,
  other file formats, undo system, installer, and everything else in the
  blueprint's roadmap — all unstarted.

## Layout

```
dino8-kernel/
  CMakeLists.txt          top-level build, fetches OpenNURBS
  include/dino8/kernel/   public wrapper headers
  src/                    wrapper implementation
  tests/                  round-trip + smoke tests
```

## Building

Requires a C++17 compiler and CMake ≥ 3.20. OpenNURBS source is pulled via
`FetchContent` at configure time (network access required) rather than
vendored, so this checks out and pins to a specific OpenNURBS tag.

```
cmake -S dino8-kernel -B dino8-kernel/build
cmake --build dino8-kernel/build
ctest --test-dir dino8-kernel/build --output-on-failure
```

OpenNURBS' actual CMake target name (`opennurbsStatic`, not the commonly
assumed `opennurbs_public`) and a real link-order bug between it and its
own `zlib` dependency are both handled already in `CMakeLists.txt` — see
the comment there if a future OpenNURBS version renames its targets again.

## Why this shape

- The wrapper types (`dino8::kernel::NurbsCurve`, `NurbsSurface`, `Brep`)
  intentionally mirror OpenNURBS' own object model rather than inventing a
  parallel one — later chunks (mesh booleans, SubD-to-NURBS conversion)
  need direct access to the underlying `ON_*` objects, not just a
  simplified facade.
- File I/O goes through `ONX_Model`, OpenNURBS' own model container, so
  `.3dm` compatibility comes from OpenNURBS directly instead of being
  reimplemented.

## Known gaps / next chunk's problem

- No general solid construction — see "What's still not done" above.
- No SubD support yet — OpenNURBS has `ON_SubD` but this chunk doesn't
  wrap it.
- No tolerance-management policy defined yet; wrapper calls use
  OpenNURBS defaults or an ad-hoc constant (`Mesh::MergeAndWeld`'s
  default tolerance), which will need revisiting once real modeling
  tolerances are decided.
