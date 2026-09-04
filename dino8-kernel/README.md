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

- `Mesh::ExtrudeCappedSolid()` closes the "trimmed face can't feed
  `BooleanCombine()`" gap the previous section left open — generally,
  not with a one-off shape. Rather than hand-deriving matching wall
  geometry for a specific profile (a real cylinder's disk caps, say),
  it extracts a cap mesh's boundary loop directly from its own triangle
  adjacency (an edge used by exactly one triangle is a boundary edge),
  so it works on *any* cap shape — including `TrimmedPlanarFace()`'s
  jagged, whole-cell-in/out trim boundary, not just a clean curve. Top,
  bottom, and wall vertices at every seam reuse the cap's own vertex
  positions exactly (no `MergeAndWeld()` tolerance involved). Verified
  in `tests/test_basic.cpp`: an untrimmed square extruded into a solid
  matches its exact area×height volume; the same trimmed face from the
  section above, extruded, matches its exact trim-area×height volume;
  and — the real proof this is watertight, not just numerically
  close — `BooleanCombine()` accepts the extruded trimmed solid and a
  disjoint union with it produces exactly the expected combined volume
  (Manifold would reject a non-manifold mesh outright, not silently
  produce a wrong answer).
- `Mesh::Cylinder()` closes the "no general primitive library" gap using
  the two general-purpose pieces above rather than adding a third
  special case: a circular disk cap (`TrimmedPlanarFace()` with an N-gon
  trim polygon) swept via `ExtrudeCappedSolid()`. First built against
  whole-cell trimming, which — verified directly, not assumed —
  systematically *under*-represents a curved boundary: 48 trim/grid
  divisions measured a real 7% volume error, 200 measured ~1.5%. That's
  what motivated `TessellateGridClippedExact()` below; `Cylinder()` now
  uses it and gets ~0.64% error at just 32 divisions.
- `NurbsSurface::TessellateGridClippedExact()` (originally
  `TessellateGridClippedConvex()` — renamed once it stopped being
  convex-only, see below) fixes the accuracy problem at its root instead
  of trading it for resolution: real polygon clipping per grid cell,
  rather than whole-cell in/out. A convex `trim_polygon` goes through the
  original Sutherland-Hodgman path (what `Cylinder()`'s circular trim and
  everything else exercising exact clipping so far actually depends on);
  a concave one now goes through a general Greiner-Hormann-style polygon
  intersection with ear-clipping triangulation, rather than being
  rejected. `Brep::TrimmedPlanarFace()` takes an `exact_clip` flag to opt
  into this per face; default stays `false` so existing whole-cell tests
  and their hand-derived counts don't change under them.
  **Getting the concave path right took three real bugs, each caught by
  hand-derived expected values, not assumed correct:** (1) a trim vertex
  landing exactly on a tessellation grid line corrupted the traced
  boundary — worked around by choosing test coordinates off exact grid
  lines, the same discipline other tests here already follow, and
  documented as a known limitation of the concave path rather than fully
  hardened against; (2) the initial always-forward polygon trace started
  from *any* unvisited crossing rather than only from an "entry" crossing
  (where the subject path moves from outside the clip region to inside),
  which for an exit-started trace walked almost the entire boundary
  instead of the small local intersection — caught by a measured area
  many times larger than the true trim area; (3) even after fixing the
  trace, a test extruded the resulting concave cap in the same direction
  as its own natural outward normal (`ExtrudeCappedSolid()` requires the
  offset to point away from the cap's normal, as
  `TestExtrudeUntrimmedFaceIntoSolid` already documented) — the resulting
  solid was a valid, Manifold-accepted closed mesh, just consistently
  wound "inside out," caught by a volume that was the exact negative of
  the expected one, not a magnitude mismatch. Verified end to end: the
  dart's exact-clipped area matches its hand-derived shoelace-formula
  value, the extruded solid's volume matches area × height, and
  `BooleanCombine()` accepts it as watertight in union with a disjoint
  box.
  **Found and fixed a real bug building this**, the kind only exercised by
  a genuinely curved, many-cell trim (a rectangle's 4 straight edges
  never hit it): clipping a grid cell near a trim-polygon vertex can
  emit a vertex numerically coincident with another one already in the
  clipped result, producing a zero-area "sliver" triangle. Left in, this
  is not just cosmetic — it corrupts `ExtrudeCappedSolid()`'s boundary-
  edge extraction, and was caught exactly that way: a real cylinder cap's
  vertex/face/boundary-edge counts failed the `3·faces + boundary_edges`
  parity every valid triangulated disk must satisfy (confirmed by hand
  computation, not assumed), and `Manifold` correctly rejected the
  resulting solid as non-manifold rather than silently accepting bad
  geometry. Fixed by deduplicating near-coincident consecutive clip
  points and skipping any resulting near-zero-area triangle; reverified
  the same parity check now holds.
- `Brep::TrimmedPlanarFace()` gained a `hole_loops_uv` parameter, closing
  the "a face with an interior hole isn't supported" gap from the
  previous section — an annulus/washer face (whole-cell path only; throws
  if combined with `exact_clip=true`, since Sutherland-Hodgman clips
  against one convex region and has no "subtract another region" mode).
  This was also the first real test of a claim made two chunks ago and
  never actually exercised: that `ExtrudeCappedSolid()`'s boundary-edge
  extraction works on "any cap shape" because it only looks at triangle
  adjacency, with no assumption baked in about how many separate boundary
  loops there are. It does — verified, not just asserted this time: an
  annulus cap (outer square minus an off-grid inner square hole) extrudes
  to a genuinely closed tube with independently-walled outer and inner
  boundaries, checked with the same hand-derived-exact-numbers standard
  as the rest of this file (40 vertices, 40 triangles, area 20 — all
  computed by hand from the whole-cell grid semantics before running
  anything, not fit to the output after) and the same Manifold-acceptance
  watertightness proof as the other `ExtrudeCappedSolid()` tests.
- `ExtrudeCappedSolid()` now validates its input instead of trusting it:
  every boundary vertex must have exactly one outgoing and one incoming
  boundary edge (a set of simple, disjoint closed loops) or it throws
  `std::invalid_argument` — rejecting a self-intersecting/"bowtie"
  boundary and an already-closed cap (nothing to sweep into walls)
  outright, rather than silently emitting overlapping or malformed wall
  geometry the way the previous section's gap list warned it could.
  Verified with two deliberately bad inputs: a real bowtie mesh (two
  triangles sharing one vertex but no edge) and `Box()`'s own already-
  closed mesh, both correctly rejected.
- `Mesh::Cone()` closes one of the two remaining "no cone, revolve, or
  loft" primitive gaps. Rather than a new special case, it reuses
  `Cylinder()`'s disk-cap construction (factored out into a shared
  `BuildCircularDiskCap()` helper) and closes it off with a new
  `Mesh::ConeToApex()` — the cap as the base, one triangle per boundary
  edge to a single new apex vertex instead of `ExtrudeCappedSolid()`'s
  translated-copy wall quads. `ConeToApex()` shares
  `ExtrudeCappedSolid()`'s boundary-edge extraction and validation
  (factored into `Mesh::ExtractValidatedBoundaryEdges()`) instead of
  duplicating it, so the bowtie/already-closed rejection applies here
  too — verified directly (`TestConeToApexSharesBoundaryValidation`), not
  just assumed to carry over from the refactor. Cone's wall-triangle
  winding was derived, not guessed, from `ExtrudeCappedSolid()`'s own
  already-proven wall winding: collapsing its two wall triangles per edge
  to a single apex point degenerates one of the two to zero area, and the
  surviving one's vertex order is exactly `(a, apex, b)`. Volume verified
  within 1% of the exact `(1/3)*pi*r^2*h` at the same resolution
  `Cylinder()` uses, plus the same Manifold-union watertightness proof.
- `Mesh::RevolveProfile()` closes the "no revolve" gap: a lathe operation
  that sweeps a 2D `(radius, height)` profile fully around an axis into a
  closed solid, built directly from the profile's own rings and bands
  rather than through `Brep`/`NurbsSurface` at all. Scoped narrower than a
  fully general revolve on purpose: the profile's first and last points
  must have radius 0 (on-axis), closed with a triangle fan to a shared
  apex vertex the same way `ConeToApex()` closes a cap - a profile needing
  a flat end rim instead (a plain cylinder, say) already has `Cylinder()`
  for that shape, so this doesn't duplicate it. Throws
  `std::invalid_argument` on an off-axis end or a profile with fewer than
  3 points, verified directly.
  The band winding was derived from this codebase's one standing rule
  (`u_dir x v_dir` = outward normal, the same rule `TessellateGrid()`,
  `Box()`, and everything else here already follows) rather than by
  analogy to `ExtrudeCappedSolid()`'s different vertex-indexing scheme:
  parameterizing a band between two rings by (tangential, height) and
  checking `tangential_dir x height_dir` at a concrete point gives the
  radially-outward direction, confirming the winding
  `tri1=(a,a2,b2), tri2=(a,b2,b)`. At either on-axis end this same formula
  degenerates one triangle per pair to zero area; the surviving one
  matches `ConeToApex()`'s own `(a, apex, b)` winding, which is a
  corroboration of the derivation, not a second independent guess.
  Verified against a genuinely independent closed-form check - not reusing
  `Cone()`'s own volume math - a "bicone"/football profile (on-axis apex →
  max radius at mid-height → on-axis apex, i.e. two cones glued base to
  base) whose exact volume, `2*(1/3)*pi*r^2*half_height`, comes from
  separately-known geometry, plus the same Manifold-union watertightness
  proof as every other solid here.
- `Mesh::LoftClosedRings()` closes the "no loft" gap: skins a sequence of
  same-vertex-count closed polygonal cross-sections ("rings") into a
  closed solid, generalizing `RevolveProfile()`'s bands (a special case
  where every ring happens to be a regular polygon approximating a circle)
  to rings of any shape, size, and vertex count as long as they all match
  in count. Throws `std::invalid_argument` on fewer than 2 rings or
  mismatched vertex counts across rings, verified directly.
  Reused `RevolveProfile()`'s band-winding derivation directly rather than
  re-deriving it - that derivation only assumed "ring i is
  CCW-as-seen-from-ahead, ring i+1 is the next one along the loft
  direction," which holds for any same-vertex-count ring pair, not just
  circular ones. The two end caps needed their own derivation (a cap is a
  different shape than a band): the first ring's cap is wound backward
  relative to the last ring's, so its normal points away from the loft
  body, like `Cylinder()`'s base disk needing `-n` while the sweep goes
  `+n`.
  Verified against a genuinely independent closed-form check, not reusing
  any other primitive's volume math: a frustum between a small square
  (area 4) and a large square (area 36), both centered on and scaled
  uniformly about the same axis - since the straight-line connection
  between corresponding vertices of two similar, parallel, coaxial
  polygons is exactly a frustum of a real pyramid (every lateral edge,
  extended, meets at one common apex), the standard closed-form frustum
  volume `(h/3)*(A1+A2+sqrt(A1*A2))` applies exactly, not approximately -
  unlike every circular-trim test in this file, which can only get within
  a percent of the true value. Measured volume matched to `1e-9`, an
  exact-value check in the same tier as the `Box()`/annulus tests, plus
  the same Manifold-union watertightness proof as every other solid here.
- `LoftClosedRings()`'s end caps now handle a **concave** ring too, not
  just convex ones (the gap the previous section's chunk left open on
  purpose). The original plain-fan-from-vertex-0 caps are replaced by
  ear-clipping (new `TriangulatePlanarRing()` in `mesh.cpp`): compute each
  ring's own Newell normal (valid for concave, not just convex, planar
  polygons - unlike a single 3-point cross product, which can pick the
  wrong sign or degenerate on nearly-collinear points), project onto that
  normal's own 2D basis, and triangulate. Rather than a second
  triangulator, this reuses the exact same
  `dino8::kernel::detail::EarClipTriangulate()` `TessellateGridClippedExact()`'s
  concave-trim path already uses - moved out of `surface.cpp` into a new
  shared header, `include/dino8/kernel/detail/polygon2d.h`, specifically
  so this wouldn't need a second copy of that logic. Verified with a real
  concave test the earlier convex frustum test couldn't cover: the same
  five-vertex concave dart used elsewhere in this file, lofted between
  two identical, simply-translated copies - since that's a true prism
  regardless of cross-section shape, its volume must equal the dart's
  known shoelace area (0.404) times height (1) exactly, not just
  approximately (measured to `1e-6`), plus the same Manifold-union
  watertightness proof as every other solid here.
- `dino8::kernel::SubD` (new file, `include/dino8/kernel/subd.h` /
  `src/subd.cpp`) wraps `ON_SubD` — OpenNURBS' **real, working**
  Catmull-Clark subdivision surface implementation, closing chunk 1's
  original "no SubD support yet" gap. Verified directly against the
  v8.34 source before relying on it, the same discipline that caught
  `ON_Brep::CreateMesh`/`ON_Surface::CreateMesh` as unimplemented stubs
  in chunk 2: `ON_SubD::BrepForm()`/`GetSurfaceBrep()` (the path that
  would convert a SubD to a real Brep/NURBS) is *also* a stub —
  `BrepForm()` is literally `return nullptr;` in the public source — but
  `ON_SubD::GlobalSubdivide()` is genuine, non-stub Catmull-Clark
  refinement: real face-point/edge-point/vertex-point computation,
  confirmed by reading the implementation, not just calling it and
  hoping. `SubD::FromControlMesh()` builds a level-0 control cage
  directly from an existing closed `Mesh`'s own faces
  (`ON_SubD::CreateFromMesh`); `Subdivide(levels)` applies real global
  subdivision in place; `ToApproximateMesh()` extracts the current
  level's control net as a `Mesh` (`ON_SubD::GetControlNetMesh`).
  **This is deliberately not exact limit-surface evaluation** — OpenNURBS'
  public API has no exact limit mesher, only the control net at whatever
  level you've subdivided to; repeated subdivision is the standard
  "just refine a lot" approximation used before a real limit evaluator,
  and is documented here as exactly that, not oversold as the real thing.
  Verified with hand-derived exact numbers, not just plausible ones: a
  6-quad box control cage has `V=8, E=12, F=6` (Euler-checked: `8-12+6=2`);
  Catmull-Clark's own rule (`V_new = V+E+F`, `F_new` = 4x once all-quad)
  gives level 1 `V=26, F=24` and level 2 `V=98, F=96` — both measured
  exactly. Volume was *not* assumed to stay near the cube's 8: probing
  levels 1-5 showed it dropping `8 → 3.5 → 2.80 → 2.66 → 2.63 → 2.62`,
  converging (not diverging) toward roughly a third of the cube's volume —
  real behavior for a cube's 8 valence-3 extraordinary corners under
  Catmull-Clark, confirmed by the monotonic, stabilizing trend rather than
  assumed to be a bug or "close enough to 8." The same Manifold-union
  watertightness proof as every other solid here passes on the subdivided
  mesh too.
- `Mesh::SaveObj()` writes a plain-text Wavefront `.obj` file - the first
  "other file format" alongside `.3dm`, so anything built here can
  actually be opened and looked at in an ordinary 3D viewer (Blender,
  MeshLab, etc.) instead of only ever being verified by its own numbers.
  A quad face (`ON_MeshFace::IsQuad()`) is written as one 4-index `f`
  line, not split into two triangles - OBJ supports n-gon faces natively.
  `Mesh::LoadObj()` completes the round trip: reads `v`/`f` lines back
  (accepting both plain `f 1 2 3` and `f 1/1/1 2/2/2 3/3/3`
  vertex/texture/normal index triples, ignoring texture/normal indices -
  this kernel's `ON_Mesh` has nowhere to put them), and deliberately
  rejects rather than guesses at anything it can't represent exactly: a
  face line with more than 4 or fewer than 3 indices (`ON_MeshFace` only
  holds a triangle or quad - silently fan-triangulating a 5-gon would
  change its meaning without telling the caller), or a face referencing a
  vertex index that hasn't appeared yet. Verified with a full round trip
  (write a known 6-quad box, reload it, check vertex/face counts *and*
  volume match exactly - proving quad faces survive as quads, not
  silently reinterpreted) and three deliberately bad inputs (a missing
  file, a forward vertex reference, a 5-index face line), all correctly
  rejected.
- **Found and fixed a real bug in `Mesh::Area()`**, the kind only exercised
  by a genuinely quad-faced mesh (every tessellator in this file emits
  triangles only, so no earlier test ever built one): `Area()` computed
  only a face's first triangle (`vi[0], vi[1], vi[2]`) and silently
  ignored `vi[3]` entirely for a real, non-degenerate quad - returning
  exactly half the true area - while `Volume()` right next to it already
  handled `IsQuad()` correctly. Surfaced while writing a SubD test that
  needed `Area()` to work on `SubD::ToApproximateMesh()`'s genuinely-quad
  output. Fixed to sum both triangles for a quad face, same convention
  `Volume()` already used; verified with a single hand-built 3×2 quad
  face whose true area (6.0) the old code would have reported as 3.0.
  This also made a second SubD verification possible: a flat 2×2 grid of
  quads (no extraordinary *interior* vertex - its one interior vertex has
  the regular valence-4) stays exactly on its own plane after
  subdivision, confirming boundary/regular-valence subdivision doesn't
  warp flat geometry - but its area still measurably shrinks (4.0 → 3.6875,
  measured, not assumed to stay exact), since a grid's 4 corners are
  themselves a kind of extraordinary vertex (valence 2) that Catmull-Clark
  pulls inward - the same qualitative effect behind the box's much larger
  volume shrink, here affecting only 4 vertices instead of every neighbor
  of 8 corners.
- `Mesh::SaveStl()` writes an ASCII Wavefront `.stl` file - the second
  "other file format" alongside `.obj`/`.3dm`, for the specific
  tools/workflows that want STL rather than OBJ (3D printing slicers in
  particular). STL is triangle-only and has no shared vertex list (each
  facet repeats its own 3 positions), so a quad face
  (`ON_MeshFace::IsQuad()`) is split into its two triangles as two
  separate facets, and each facet's normal is computed directly from its
  own 3 vertices rather than written as the permitted-but-useless
  all-zero placeholder. Export only, same as `.obj` before `LoadObj()`
  was added. Verified by writing the known 6-quad box and re-parsing the
  file: exactly 12 facets (2 per quad, not 6), and the first facet's
  normal matches the bottom face's known outward direction `(0,0,-1)`
  exactly, not a placeholder or an arbitrary sign.
- `Mesh::Torus()` is a genuinely new primitive shape, not a
  reparameterization of an existing one: `RevolveProfile()`'s profile
  must start and end on the axis, but a torus's circular cross-section
  never touches the axis at all, so it doesn't fit that method's scope.
  Built directly as a `major_segments` x `minor_segments` quad grid that
  wraps in *both* directions - unlike `Cylinder()`/`Cone()`, a torus has
  no boundary anywhere, so no end caps or `ExtrudeCappedSolid()`/
  `ConeToApex()` call is needed at all; the grid is already closed by
  construction. The winding was derived independently (parameterizing by
  major/minor angle and evaluating the two tangent directions' cross
  product at the tube's outer equator gives the radially outward
  direction, confirming the same cell-winding convention
  `TessellateGrid()` already uses), not copied from Cylinder's or
  RevolveProfile's derivation, since neither actually applies here.
  Verified within 1% of the exact `2*pi^2*major_radius*minor_radius^2`
  volume, plus the same Manifold-union watertightness proof as every
  other solid here - a meaningful check specifically because a
  both-directions-wrapping grid has more ways to end up non-manifold than
  a grid with a boundary does.
- `Mesh::GetBoundingBox()` (new `BoundingBox` struct in `types.h`) closes
  a real, if narrow, gap: nothing earlier here could answer "roughly how
  big/where is this," which any future viewport (camera framing) or
  spatial query (a coarse overlap test before a real boolean) needs.
  Throws `std::invalid_argument` on a mesh with no vertices rather than
  returning a misleading all-zero box that would look like a valid
  point-sized mesh at the origin. Verified with an asymmetric box (a
  different extent on each axis, so a bug mixing up which axis feeds
  which output component would be caught) and the empty-mesh rejection.
- `TessellateGridClippedExact()` now actually validates that
  `trim_polygon` is simple (non-self-intersecting) - a requirement this
  file documented since the concave-clipping chunk but never checked
  until now. New `dino8::kernel::detail::IsSimplePolygon()` (and its
  `SegmentsProperlyIntersect()` helper) in `polygon2d.h` tests every pair
  of non-adjacent edges for a genuine crossing - a deliberately narrower
  check than "these two segments share any point," so it catches a real
  self-intersecting polygon without also flagging the ordinary case of
  adjacent edges sharing an endpoint. `TessellateGridClippedExact()` now
  throws `std::invalid_argument` up front rather than clipping against an
  ill-formed input and returning whatever the clipper happened to
  compute. Verified with a genuine bowtie trim (a quadrilateral's 4
  corners listed in crossed order) being correctly rejected; confirmed no
  regression by re-running the full suite, since every trim polygon any
  earlier test used (rectangles, the dart, the annulus's outer/hole
  loops, `Cylinder()`'s circle) is already simple.
- `LoftClosedRings()`'s first and last rings get the same simplicity
  check, since they're each ear-clipped into an end cap the same way
  `TessellateGridClippedExact()`'s `trim_polygon` is. **A first attempt at
  this reused `TriangulatePlanarRing()`'s existing Newell-normal
  projection for the check and it silently failed to catch anything** -
  measured, not assumed correct: a hand-built bowtie ring test still
  passed validation. The bug: a self-intersecting polygon's two "lobes"
  wind in opposite senses, so their Newell-method contributions can
  cancel to exactly zero (confirmed by hand for the specific bowtie used)
  - projecting onto a degenerate zero normal produces garbage 2D
  coordinates the simplicity check trivially passes. Fixed with a
  separate, cruder normal specifically for this check
  (`IsPlanarRingSimple()`): scan consecutive point triples for the first
  with a non-negligible cross product (any two non-parallel edges), since
  self-intersection is preserved under projection onto any plane
  containing the points, regardless of which of the two normal directions
  is picked - unlike triangulation winding, this check doesn't care which
  way the normal points, so it doesn't need Newell's winding-consistency
  guarantee, only a valid plane. Re-verified with the same bowtie ring,
  now correctly rejected.
- `Mesh::GetCentroid()` complements `GetBoundingBox()`: the volume-weighted
  center of mass (uniform density assumed), computed via the same
  divergence-theorem decomposition `Volume()` already uses - each
  triangle (plus the origin) forms a tetrahedron whose centroid is the
  average of its 4 vertices and whose signed volume `Volume()` already
  sums per-triangle; the mesh centroid is the volume-weighted average of
  those. Only meaningful for a closed, consistently-oriented mesh, the
  same requirement `Volume()` has (`GetBoundingBox()` needs no such
  assumption, since it's a plain vertex extent). Throws
  `std::invalid_argument` on a mesh with near-zero volume rather than
  dividing by it. Verified with the same asymmetric box `GetBoundingBox()`
  used: its centroid is exactly the midpoint of each axis's extent, by
  symmetry, giving a clean hand-derivable exact match rather than a
  tolerance-based one.
- `Mesh::Transform()` closes a real gap every earlier primitive had:
  nothing here could move, rotate, or scale a mesh once built - each
  primitive could only be positioned via its own constructor parameters
  (`Cylinder()`'s `base_center`/`axis`, say), with no general way to
  reposition the result afterward. Delegates directly to `ON_Mesh::
  Transform` - verified as a real, working implementation (not a stub
  like `ON_Brep::CreateMesh`) before relying on it - rather than
  reimplementing per-vertex transformation here. Deliberately just one
  method taking an `ON_Xform`, not separate `Translate()`/`Rotate()`/
  `Scale()` wrappers: OpenNURBS' own `ON_Xform::TranslationTransformation`/
  `ON_Xform::ScaleTransformation`/`ON_Xform::Rotation` already build
  exactly those, and this header already includes `<opennurbs.h>` to use
  them. Verified with hand-derivable exact results for all three:
  translating shifts the bounding box by exactly the offset (volume
  unchanged); scaling by 2 about the origin exactly doubles the bounding
  box and multiplies volume by `2^3 = 8`; rotating about an arbitrary
  axis/center preserves volume exactly, a real geometric invariant, not a
  coincidence of one particular test box.
- `BooleanOp::SymmetricDifference` adds a fourth boolean operation (the
  region in exactly one of the two inputs, not both). Manifold itself has
  no direct XOR primitive - only add/subtract/intersect - so this is
  computed as `Union(a, b) - Intersection(a, b)` (three Manifold calls
  chained through `BooleanCombine()` itself rather than a fourth
  from-scratch implementation). Verified against the volume from an
  independent formula for the same operation, `(A-B) + (B-A)`, not just
  self-consistency with the implementation being tested: two boxes with
  volume 8 each and a 1×1×1 overlap give `(8-1)+(8-1) = 14` by that
  formula, matching what `Union - Intersection` computes.
- `SubD::FromControlMesh()` gained a `crease_at_double_edges` parameter,
  closing part of the "no crease support" gap this section had flagged.
  OpenNURBS defines an interior SubD crease at a mesh "double edge": an
  interior edge where two adjacent faces reference *distinct* vertex
  indices at coincident 3D locations, rather than sharing one vertex
  index (verified directly against the v8.34 source's own definition of
  `ON_SubDFromMeshParameters::InteriorCreaseOption::AtMeshDoubleEdge`,
  not guessed at from the name) - passing `true` switches
  `ON_SubD::CreateFromMesh`'s parameters from `Smooth` to
  `InteriorCreases`, which detects exactly that pattern. A caller wanting
  a sharp fold along some edge duplicates that edge's two vertices on one
  of the two faces meeting there; ordinary shared-index construction (as
  every other primitive in this kernel builds) never produces a double
  edge, so this is opt-in with no effect on existing meshes.
  Verified with a real, measured behavioral difference, not just "it
  didn't crash": a two-quad "hinge" mesh (two 1×1 quads folded 90° along a
  shared edge, that edge double-vertexed) subdivided once with
  `crease_at_double_edges=true` puts a genuine new subdivision point
  exactly at the fold's straight-line midpoint `(0.5, 0, 0)` - the same
  "creases/boundaries subdivide to stay exactly on their own line" rule
  already verified for real mesh boundaries - while the same mesh without
  the flag treats that edge as smooth and Catmull-Clark visibly rounds the
  fold, pulling that point measurably off the line instead. Both cases
  first confirmed to weld the double edge's coincident-but-distinct
  indices into the same 6 SubD vertices either way - the flag only changes
  that edge's tag (smooth vs. creased), not whether the mesh recognizes
  the coincident points as one topological vertex.
- `NurbsCurve::Length()` is a from-scratch arc-length approximation
  (polyline sampling over the curve's own parameter domain), not a
  wrapper - verified directly against the v8.34 source, not assumed, that
  OpenNURBS' public `ON_Curve` API has no `GetLength()`/arc-length method
  at all (grepped the whole source tree, not just this one class), the
  same "declared for Rhino, not present in the public build" pattern
  chunk 2 found for `ON_Brep::CreateMesh` and this file found for
  `ON_SubD::BrepForm`. A polyline's chords always understate a smooth
  curve's true length, so it converges to the true length from below as
  the sample count increases; for a straight-line curve (no curvature to
  approximate away) it's exact at any sample count. Verified with a
  straight-line curve giving the exact 3-4-5 distance (5.0) at both a
  small and a large sample count, and with a genuinely curved curve's
  measured convergence: length increases monotonically (never decreases
  or oscillates) as sample count grows 10x at a time, and the increase
  per step shrinks each time - a real converging trend, not assumed.
- `RevolveProfile()` closes the "no flat end rim" gap this section used to
  flag: an end whose radius is 0 still gets the original on-axis apex fan,
  but an end with nonzero radius now gets a flat circular disc cap instead
  of being rejected outright. The cap is a plain center-vertex fan (not a
  reuse of `BuildCircularDiskCap()`'s NURBS-surface-trimmed disc that
  `Cylinder()`/`Cone()` use), oriented by the same derivation this file
  always uses for winding: a fan triangle `(center, k, k+1)` in increasing-
  theta order has normal `+axis` (from `(ring[k]-center) x (ring[k+1]-
  center) = r^2*sin(dtheta)*(ex x ey)`, `ex x ey = axis`, `dtheta > 0`), so
  the *end* cap uses that order directly and the *start* cap reverses it
  to get the `-axis` outward normal a start cap needs. Also lowered the
  minimum profile length from 3 points to 2, since a flat-capped profile
  no longer needs an on-axis point to force a fan at all - the smallest
  useful shape is now a single band between two flat-capped rings.
  Verified with three independent closed-form/cross-check volumes, not
  just "it builds a watertight solid": a base-first flat-capped cone
  (built in the *opposite* order from `Cone()`'s own apex-last
  construction, so this genuinely re-derives the cap's orientation rather
  than reusing it) measurably converges toward `(1/3)*pi*r^2*h` as
  `revolve_segments` increases; a frustum with both ends off-axis and at
  different radii matches `(pi*h/3)*(r1^2+r1*r2+r2^2)` to within 1%; and
  the degenerate case `r1 == r2` (a cylinder built via two flat-capped
  rings instead of `ExtrudeCappedSolid()`) matches `Mesh::Cylinder()`'s
  own independently-built volume to within 0.1%, despite the two using
  completely different cap implementations. All three also confirmed
  watertight via a real Manifold union, same as every other closed-solid
  primitive here.
- `LoftClosedRings()` closes the other half of the gap this section used
  to flag for its end rings: planarity, alongside the simplicity check it
  already had. `IsRingPlanar()` finds a normal the same way
  `IsPlanarRingSimple()` does (scanning consecutive point triples for the
  first non-degenerate cross product - three points from the ring itself
  already pin down the only plane a genuinely planar ring could lie in),
  then rejects any ring with a point whose out-of-plane distance exceeds a
  tolerance scaled by the ring's own size (relative, not absolute, the
  same reasoning `MergeAndWeld()`'s tolerance already uses). Verified with
  a square ring with one corner pulled 0.3 units out of its own plane
  (against a ~1.4-unit diagonal, far past the 1e-6-relative tolerance) -
  `LoftClosedRings()` now throws `std::invalid_argument` on it instead of
  silently ear-clipping a Newell-normal projection that wouldn't reflect
  the ring's actual 3D shape.
- `Mesh::ComputeVertexNormals()` closes part of the ".obj has no normals"
  gap this section used to flag: a real per-vertex smoothing normal (the
  area-weighted sum of every adjacent face's own flat triangle normal,
  normalized - not a placeholder or a plain unweighted average), and
  `SaveObj()` now writes one `vn` line per vertex and references it from
  every face corner in `v//vn` form, so a viewer gets actual smooth
  shading instead of falling back to its own flat per-facet normals.
  `LoadObj()` still only reads `v`/`f` lines - a face's geometry is
  already fully determined by its vertex indices alone, so round-tripping
  through `SaveObj()`/`LoadObj()` reproduces the same geometry (and the
  same normals, recomputed from it) but not necessarily the same file
  bytes. Verified with two hand-derivable exact cases: a unit-cube
  corner's normal (three adjacent unit-square faces, so equal-weighted)
  is exactly `(-1,-1,-1)/sqrt(3)`; every corner of a single flat quad gets
  exactly that quad's own flat normal, with no neighbors to average
  against. `.obj` still carries no texture coordinates, materials, or
  groups.
- `TessellateGridClippedExact()`'s concave path (`ClipPolygon`) closes the
  "trim vertex on a grid line" degeneracy this section used to flag:
  `ClipPolygon`'s own crossing detection deliberately excludes an
  intersection landing within its own epsilon of a segment endpoint (the
  standard way to avoid double-registering a crossing at a shared
  vertex), but that exclusion misfired whenever a trim vertex happened to
  land exactly on a cell's grid line - the cell edge lying along that line
  hit the trim edge right at its endpoint and got excluded as "not a real
  crossing," corrupting that cell's clipped topology. Fixed with the
  standard "simulation of simplicity" technique: before clipping, nudge
  any trim vertex within `1e-6` (relative to one cell's width) of a grid
  line off of it by `1e-6` of that width - a shape change far below this
  function's own duplicate-point epsilon, let alone any caller's area
  tolerance. Only applies to the concave path (`ClipConvex`, used for a
  convex trim, has no such exclusion). Verified by reproducing the exact
  case `TestExactClippingHandlesNonConvexTrim`'s own comment already
  documented as broken (a dart's reflex vertex at u=0.5, exactly on the
  8-division grid's own u=0.5 line): confirmed this test genuinely fails
  without the fix (wrong area, not just "didn't crash") by temporarily
  reverting only the fix and re-running it, then confirmed it passes with
  the fix restored - not just a fix asserted from reading the code.

## What's still not done (as of chunk 2)

- `Brep::Box()`, `Brep::Sphere()`, `Brep::TrimmedPlanarFace()`
  (+ `hole_loops_uv`) + `Mesh::ExtrudeCappedSolid()`/`Mesh::Cylinder()`/
  `Mesh::ConeToApex()`/`Mesh::Cone()`/`Mesh::RevolveProfile()`/
  `Mesh::LoftClosedRings()`/`Mesh::Torus()` are the only shapes/operations
  here.
  `RevolveProfile()` now supports a flat end rim too (see below);
  `LoftClosedRings()`'s end caps require each ring to be planar and
  simple (non-self-intersecting) - both are now validated
  (`IsRingPlanar()`/`IsPlanarRingSimple()`, thrown as
  `std::invalid_argument` on the first/last ring), and concave rings are
  handled correctly (see above).
- `TessellateGridClippedExact()` now handles a concave `trim_polygon` too
  (via a general Greiner-Hormann-style clipper, see above), but that path
  is newer and more narrowly tested than the long-proven convex one: a
  trim vertex landing exactly on a tessellation grid line is now handled
  (see above), but only two concave shapes (a five-vertex dart, in two
  variants) have actually been exercised so far — a genuinely pathological
  concave polygon (many reflex vertices, features much smaller than the
  grid resolution) hasn't been. `trim_polygon` must
  still be simple (non-self-intersecting) - that *is* now validated
  (`dino8::kernel::detail::IsSimplePolygon()`, thrown as
  `std::invalid_argument`).
- The trim-polygon test in `TessellateGrid()` is whole-cell in/out
  (a cell is kept only if all four corners are inside), not real boundary
  clipping — a curved or diagonal trim edge will look faceted/staircased
  at low resolution, not smooth.
- `Mesh::MergeAndWeld()`'s tolerance-based vertex snapping is now also
  validated against the specific case this section used to flag as open:
  two *independently constructed* single-face Breps (not faces sharing
  one Brep's own control points) — one square, and an adjacent one that's
  degree-elevated (bilinear → bicubic) after construction, so its shared
  edge is evaluated through different floating-point arithmetic than the
  first square's, not merely re-reading the same literal values. Verified
  with hand-derived exact numbers in `tests/test_basic.cpp`: two 4×4-grid
  squares (25 raw vertices each) weld down to exactly 45 (the 5 shared
  edge points collapsed once each), with area exactly 2.0. This still
  doesn't cover *non-conforming* meshes (two faces tessellated at
  different resolutions along their shared edge) — vertex-snapping can't
  fix a genuine T-junction, only near-identical positions at matching
  sample counts.
- `SubD` wraps real Catmull-Clark refinement, but not exact limit-surface
  evaluation — `ToApproximateMesh()` is the repeated-subdivision
  approximation, not the true smooth surface. Interior creases are now
  supported (see below) but only that one crease option; no SubD editing
  (adding/removing faces, extrude, etc.), and no SubD ↔ Brep conversion
  (that direction is the stubbed `BrepForm()`/`GetSurfaceBrep()` this
  section already flagged).
- `.obj` support (`SaveObj()`/`LoadObj()`) round-trips geometry and now
  writes real per-vertex normals (see above), but still no texture
  coordinates, materials, or groups, and `LoadObj()` still only reads
  `v`/`f` lines. `.stl` support (`SaveStl()`) is export-only, and both are
  the only formats here - no glTF, FBX, etc.
- Adaptive/curvature-aware meshing, the viewport/display engine, GPU path
  tracer, command engine, UI shell, visual scripting, undo system,
  installer, and everything else in the blueprint's roadmap — all
  unstarted.

## Layout

```
dino8-kernel/
  CMakeLists.txt          top-level build, fetches OpenNURBS
  include/dino8/kernel/   public wrapper headers
  src/                    wrapper implementation
  tests/                  round-trip + smoke tests
  examples/               small end-to-end demo program(s)
```

## Example

`examples/build_demo_model.cpp` (built as `dino8_demo_trophy`, on by
default - set `-DDINO8_KERNEL_BUILD_EXAMPLES=OFF` to skip it) combines
several primitives - `Cylinder()`, `Torus()`, `Cone()` - through three
chained `BooleanCombine()` calls into one solid, then exports it as both
`.obj` and `.stl`. Every piece it uses is already covered by
`tests/test_basic.cpp` with hand-derived exact numbers, but nothing there
exercises them *together* - a bug in how, say, `Cylinder()`'s and
`Torus()`'s output interact under a boolean wouldn't necessarily show up
in either primitive's own isolated test. Running it confirmed the
pipeline actually composes: the union's measured volume (~36.57) is
exactly the naive sum of the four pieces' individual volumes (~37.01)
minus the deliberate stem/ring overlap - not an isolated per-primitive
number, a real cross-check on the combined result.

```
./dino8-kernel/build/examples/dino8_demo_trophy
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
