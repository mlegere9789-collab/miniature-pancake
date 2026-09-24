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
- `Mesh::FlipNormals()` closes a real gap nothing earlier here could
  answer: a mesh built (or loaded) with the wrong handedness had no way to
  correct it after the fact, since every operation here (`Volume()`,
  `ComputeVertexNormals()`, `BooleanCombine()`) assumes CCW-from-outside
  winding and just silently gives a sign-flipped or inside-out answer
  otherwise. Reverses each face's own vertex loop in place (not a
  reordering of the vertex list) - a quad's `(a,b,c,d)` becomes
  `(d,c,b,a)`, a triangle's `(a,b,c)` becomes `(c,b,a)` while keeping the
  `vi[3]==vi[2]` encoding `ON_MeshFace::IsQuad()` relies on to tell a
  triangle from a quad. Verified on both a quad-faced and a
  triangle-faced box (exercising both of the method's branches): flipping
  exactly negates `Volume()` (same magnitude, opposite sign - the
  divergence-theorem sum flips which side is "outward"), leaves `Area()`
  completely unchanged (winding-independent by construction), and applied
  twice reproduces the *exact* original volume bit-for-bit, not just an
  equivalent one - a genuine involution, since it's the same indices
  reversed back to their original order, not a fresh recomputation.
- `Mesh::IsClosedManifold()` is the first direct answer to "is this mesh
  actually a valid closed solid?" - previously the only way to find out
  was to run a boolean and see whether Manifold accepted the result,
  a side effect of an unrelated operation rather than a diagnostic in its
  own right, and one that (like every other closed-mesh operation here)
  gives no information about *why* a bad mesh is bad. Checks two
  independent conditions from the mesh's own face list alone: every edge
  borders exactly 2 faces (closed - catches both an open boundary, count
  1, and a non-manifold edge shared by 3+ faces, count > 2), and no
  directed edge is walked twice (consistent orientation - two faces
  sharing an edge but both "walking" it the same direction, rather than
  opposite ways, means one is wound backwards relative to the other).
  Verified with four cases distinguishing what actually broke: a closed
  box (quad- and triangle-faced, exercising both edge-extraction
  branches) reports true; the same box with one face deleted (a real
  hole) reports false via the edge-count condition; the same box with
  *one* face's own winding reversed (not the whole mesh) reports false
  via the orientation condition specifically, even though every edge
  still borders exactly 2 faces; and flipping *every* face
  (`FlipNormals()`) still reports true, since a globally inside-out mesh
  is still closed and consistently oriented relative to its own
  neighbors - `IsClosedManifold()` can't and shouldn't distinguish that
  from "right side out" (`Volume()`'s sign is what carries that
  information).
- `NurbsCurve::TangentAt()` fills a small gap next to `Length()`: the unit
  direction of travel along the curve at a given parameter, delegating
  directly to `ON_Curve::TangentAt` after verifying (by reading the v8.34
  source) that it's a real implementation calling through to
  `Ev1Der`/`EvTangent`, not a stub - the same discipline `Length()`
  already applied when it found `ON_Curve` had no arc-length method at
  all. Verified with a hand-derivable exact case (a straight-line curve's
  tangent is exactly its own unit direction, `(3/5, 4/5, 0)`, at every
  parameter value tested, with no curvature to introduce variation) and a
  measured one for a genuinely curved curve: `TangentAt()` agrees with a
  central-finite-difference approximation of the curve's own derivative
  at several parameter values, not just "returns some unit vector."
- `NurbsSurface::NormalAt()` is the surface counterpart to
  `NurbsCurve::TangentAt()`: the unit normal (`d/du x d/dv`, normalized)
  at a given `(u, v)`, delegating to `ON_Surface::EvNormal` after
  verifying it's a real implementation (computes the cross product of the
  two partials from `Ev1Der`, not a stub). Throws `std::runtime_error` if
  OpenNURBS itself can't evaluate a normal there (a genuinely singular
  point) rather than returning a placeholder. Verified with a
  hand-derivable exact case (a flat `P(u,v)=(u,v,0)` surface's normal is
  exactly `(0,0,1)` everywhere, since `d/du=(1,0,0)` and `d/dv=(0,1,0)`
  are constant) and a measured one for a genuinely curved surface:
  `NormalAt()` agrees (up to sign) with a finite-difference cross product
  of the surface's own partial derivatives at several `(u, v)` values.
- `Model::AddMesh()` closes a real gap in `.3dm` support: `Model` already
  had `AddCurve()`/`AddBrep()`, but every closed-solid primitive and every
  `BooleanCombine()` result here is a `Mesh`, and until now there was no
  way to put one into a `.3dm` file at all - only to export it separately
  via `Mesh::SaveObj()`/`SaveStl()`. Same pattern as the other two: copies
  the mesh's underlying `ON_Mesh` into a new model geometry component.
  Verified with a real round trip, not just "the object count went up by
  one": saved a mesh, reloaded the file, walked the reloaded model's
  geometry components with `ONX_ModelComponentIterator` to find the
  actual `ON_Mesh` object inside it, and confirmed its vertex count, face
  count, and volume all exactly match the original (quad faces preserved,
  not reinterpreted).
- `Model::AddSubD()` closes the same gap for `SubD` that `AddMesh()`
  closed for `Mesh` - `ON_SubD` is also an `ON_Geometry` subclass, so the
  same "copy the underlying OpenNURBS object into a new model geometry
  component" pattern applies directly. Verified the same way: saved a
  SubD control cage, reloaded the file, found the actual `ON_SubD` object
  inside the reloaded model's geometry components, and confirmed its
  vertex and face counts exactly match the original.
- `Model::AddPointCloud()` closes the same gap for `PointCloud` that
  `AddMesh()`/`AddSubD()` closed for their own types: `PointCloud`'s own
  header already documented that its underlying `ON_PointCloud` is "the
  same one OpenNURBS' own `.3dm` reader/writer already round-trips" - true
  of the OpenNURBS class, but until this method existed there was no way
  to get a `dino8::kernel::PointCloud` into a `Model` at all, so that
  round-trip claim was unreachable from this kernel's own API. Same
  pattern: copies the cloud's underlying `ON_PointCloud` into a new model
  geometry component. Verified with a real round trip on all three
  optional fields at once (a cloud is "all or nothing" per field, so a
  partial round trip of colors or normals would silently read back as "no
  colors"/"no normals" rather than an error): saved a 3-point cloud with
  per-point colors and per-point normals set, reloaded the file, found the
  actual `ON_PointCloud` object inside the reloaded model's geometry
  components, and confirmed point count, `HasPointColors()`,
  `HasPointNormals()`, and every point's exact position/color/normal all
  survived.
- `PointCloud::SaveXyz()`/`LoadXyz()` close a real gap: before this,
  `PointCloud` had no `Save`/`Load` of its own at all - only
  `Model::AddPointCloud()`'s `.3dm` route existed, with no counterpart to
  `Mesh::SaveObj()`/`SaveStl()` for the plain-text ASCII XYZ format most
  external point-cloud tools (CloudCompare, PCL, MeshLab) actually read
  and write. One point per line: `x y z`, or `x y z nx ny nz` when the
  cloud has normals (the columns-3-or-6 convention `LoadXyz()` also
  parses back, rejecting a file that mixes both widths as genuinely
  ambiguous rather than guessing). Per-point colors are deliberately NOT
  written: unlike position/normal, ASCII XYZ has no single agreed-on
  column order, count, or scale for color across the tools that read it,
  so writing something would be inventing a convention the format doesn't
  actually have - an honest, documented gap instead of a silent,
  undocumented one. Verified with a real round trip of exact values (not
  just point count) for a positions-only cloud and, separately, a cloud
  with normals set, plus explicit rejection tests for a nonexistent file,
  a file mixing 3- and 6-column lines, a line with a column count that's
  neither, a non-numeric token, and a file with zero points.
- `Mesh::SavePly()`/`LoadPly()` close a real gap: this kernel had zero PLY
  (Stanford Polygon) code at all before this - no export, no import,
  despite PLY being a real, commonly-used mesh interchange format
  alongside `.obj`/`.stl`. Writes ASCII PLY only (binary
  `binary_little_endian`/`binary_big_endian` PLY is a disclosed,
  out-of-scope gap - `LoadPly()` rejects a binary-format header outright
  rather than misreading it, the same honest treatment this codebase
  already gives Parasolid/ACIS licensing). Unlike `.stl`, PLY's face
  element is a genuine variable-length list, so a quad face is written as
  one native 4-index face, not split into two triangles. `LoadPly()`
  parses the header's own declared property list by name rather than
  assuming a fixed column order - tolerating extra properties this kernel
  doesn't use (e.g. color) - and reads normals but discards them (same
  "always geometry-derived" convention `LoadObj()`'s `vn` already has,
  since this kernel's `Mesh` has nowhere to store an independent
  per-vertex normal). Verified with a real round trip: reopened
  `SavePly()`'s own output and checked the header's `element vertex`/
  `element face` counts, confirmed the face element declares a genuine
  `property list` (not a fixed-size property), confirmed a quad face
  survived as a single 4-corner line, then `LoadPly()`'d it back and
  checked vertex/face counts and volume all exactly match the original -
  plus a separate round trip with texture coordinates set, and explicit
  rejection tests for a binary-format header, a vertex element missing
  `z`, a face line with the wrong corner count, and an out-of-range face
  index.
- Every `Model::Add*()` gained an optional `name` parameter, closing a
  real gap in `.3dm` metadata fidelity: before this, every object placed
  in a `Model` got a default, empty `ON_3dmObjectAttributes`, so a caller
  had no way to attach even the most basic .3dm object metadata - the
  object name Rhino itself relies on for selection-by-name and for
  round-tripping identity across a save/reload. A non-empty name is set
  via `ON_3dmObjectAttributes::SetName(..., /*bFixInvalidName=*/true)`,
  the same call `dino8-app/src/io/File3dm.cpp` already uses for every
  other named entity it writes; an empty (default) name leaves the
  attributes exactly as before, so the change is additive - no existing
  caller's behavior changes. Verified with a real round trip through an
  actual `.3dm` file: named a `Mesh` and a `Brep` differently, added a
  third `Curve` with no name at all, saved, reloaded, and confirmed each
  reloaded object's own `ON_3dmObjectAttributes::Name()` exactly matches
  what it was given - including the unnamed curve coming back with a
  genuinely empty name, not some default placeholder.
- `Model::AddLayer()` plus a new `layer_index` parameter on every
  `Model::Add*()`, closing another real gap in `.3dm` metadata fidelity
  flagged by the same PARITY_MAP.md evidence as the `name` parameter
  above: before this, this kernel had no concept of a layer at all (`grep
  ON_Layer` in `dino8-kernel/src` found nothing), so nothing it saved
  could carry Rhino's most basic organizational metadata - color-by-layer,
  per-layer visibility, selection-by-layer - even though `ONX_Model` (and
  the `.3dm` format underneath) has always supported it.
  `Model::AddLayer(name, color)` wraps `ONX_Model::AddLayer()`, OpenNURBS'
  own "easy way to add a layer" helper, and returns the new layer's index
  for use as every `Add*()`'s new `layer_index` argument; an empty `name`
  returns `-1` instead of forwarding to OpenNURBS, whose own contract for
  that case (aliasing the "Default" layer) would be a surprising silent
  success for a caller who asked to add a named layer. `layer_index`
  defaults to 0 (the model's always-present default layer, the same value
  every existing object's attributes already carried), so the change is
  additive - no existing caller's behavior changes. Verified with a real
  round trip through an actual `.3dm` file: added a named, colored layer,
  placed a `Mesh` on it by index, left a `Brep` on the default layer,
  saved, reloaded, and confirmed the reloaded layer's name and color
  exactly match what `AddLayer()` was given, the mesh's reloaded
  `ON_3dmObjectAttributes::m_layer_index` matches the returned index, and
  the brep's stayed at 0.
- `Brep::GetTightBoundingBox()` closes a real gap: nothing here could
  answer "roughly how big/where is this Brep" without tessellating it
  first, and even then Mesh::GetBoundingBox() only sees a tessellation's
  sampled vertices - an approximation of the true curved surface, not an
  exact bound on it. Delegates to `ON_Brep::GetTightBoundingBox` -
  **correction, found one chunk later by testing a genuinely
  doubly-curved face**: despite its name, this is *not* a real tight/exact
  bound in the public OpenNURBS build. It only samples each face's
  boundary/Greville-abscissa isocurves and control points, never a
  genuine 2D interior extremum search - more subtly, exact for
  `Brep::Sphere()` (a standard rational-NURBS sphere's meridian circles
  happen to have their own extrema exactly at points the isocurve
  sampling evaluates, not because the algorithm does a real search), but
  a doubly-curved bicubic bulge surface whose true peak sits at its own
  interior center - proven by evaluating `NurbsSurface::PointAt()` there
  directly - comes back overshot at exactly double the true height
  instead. Still always a valid, safe bound (never excludes real
  geometry, only occasionally overshoots), just not the minimal one its
  name promises - the same "declared for Rhino, degraded in the public
  build" pattern this codebase has found before (`ON_Brep::CreateMesh`,
  `ON_SubD::BrepForm`), found here by testing a case specifically chosen
  to expose it rather than assumed correct from a name and a non-stub
  function body. **Second correction, found and fixed two chunks later
  (see `GetTightBoundingBox()`'s own later, fuller entry below)**: the
  claim above that this was "exact for `Brep::Box()` (flat faces)" was
  true only because `Box()`'s own faces are untrimmed (their trim
  coincides with the surface's own full domain) - the underlying gap this
  method never consulted a face's actual trim boundary AT ALL, so a
  genuinely trimmed flat face (e.g. `FromMixedFaces()`'s own planar
  faces, or `TrimmedPlanarFace()`'s general case) came back sized to its
  UNTRIMMED surface, not its real trim - a real, silent-overestimate bug,
  not merely a documentation gap. Now fixed for exactly that provably-safe
  case; see the later entry for the full story.
- `NurbsCurve::GetTightBoundingBox()` is the curve-level analog, and hits
  the identical public-build limitation, confirmed by reading OpenNURBS'
  own source directly this time rather than discovering it by accident: `ON_BezierCurve::
  GetTightBoundingBox` (what `ON_Curve::GetTightBoundingBox` reduces to
  per Bezier span) literally calls `ON_GetPointListBoundingBox` - its own
  comment says "good enough for file IO needs in the public source code
  version." Verified with a quadratic curve whose true y-extent is
  exactly `[0, 0.5]` (confirmed via `PointAt()`): `GetTightBoundingBox()`
  returns `y_max = 1.0` instead, exactly the middle control point's own
  y-coordinate - the control-point bound, not the curve's real one. Exact
  only when a curve's true extremum happens to coincide with a control
  point or endpoint (a straight line, certain conics).
- `Mesh::ContainsPoint()` is a real, previously-missing point-membership
  query: every earlier query here (`Volume()`, `GetCentroid()`,
  `GetBoundingBox()`) describes the solid as a whole, not a specific
  point's relationship to it. Standard ray-casting: casts a ray from the
  point in the fixed `+X` direction and counts triangle crossings (via a
  textbook Moller-Trumbore ray-triangle intersection, a quad face's own
  two triangles counted independently, same split `Area()`/`Volume()`
  already use) - odd means inside. Verified on both a quad- and a
  triangle-faced box, and - more meaningfully than a plain convex solid -
  on a genuinely hollow shape built via a real `BooleanCombine()`
  difference (a box with a narrower box subtracted from its middle): a
  point in the hollow cavity correctly reads as outside despite sitting
  well inside the *outer* box's own bounding box, a real check that the
  ray-cast counts crossings through both walls rather than just doing a
  bounding-box test in disguise.
  A real degeneracy was hit and fixed while writing this test, not
  just anticipated: a test point whose (y, z) coordinates landed exactly
  at a box face's own center made the ray hit precisely the shared edge
  between that face's two split triangles - the doc comment's own
  documented "ray passes exactly through an edge" caveat - miscounting
  the crossing and failing the test. Fixed by choosing off-center,
  off-diagonal test coordinates instead of by changing the
  implementation (the degeneracy is a property of any single-ray-cast
  point-in-solid test, not a bug to code around here), confirming the
  caveat is real rather than theoretical.
- `Mesh::ClosestPoint()` answers the query `ContainsPoint()` can't: "how
  far, and to where" for a point that isn't inside. Brute-force over
  every triangle (a quad face's own two triangles counted independently,
  same split every other per-face method here uses) via the standard
  region-based point-to-triangle algorithm (Ericson's *Real-Time
  Collision Detection* 5.1.5: classify the query's projection into one of
  a triangle's 7 barycentric Voronoi regions - 3 vertices, 3 edges, 1
  interior face - via a handful of dot products, not an iterative or
  approximate search). Verified with three hand-derivable exact cases
  covering three different regions on a 2x2x2 box: a query directly above
  a face's interior projects straight down onto it exactly; a query
  beyond a corner returns exactly that corner; a query beyond an edge's
  midpoint returns exactly that point on the edge. Also checked a
  degenerate-but-meaningful case where the closest point genuinely isn't
  unique: from the cube's own center, every face is exactly 1 unit away,
  so the test only asserts the hand-derivable *distance* (1.0), not which
  of several equally-valid points comes back.
- `Mesh::SignedDistance()` combines the two: negative if `ContainsPoint()`
  says inside, positive otherwise, magnitude from `ClosestPoint()` - the
  "how far, which side" answer a CSG or offset-surface operation needs
  that neither query alone gives. Not new geometry math, just a
  composition of two already-verified primitives, so its own tests
  cross-check against hand-derived face distances (3.0 outside, 0.7
  inside at a point deliberately chosen off the box's own center to avoid
  `ContainsPoint()`'s documented ray/diagonal degeneracy) plus a sign
  check straddling a face from both sides.
- `SplitByPlane()` closes a real gap none of this kernel's own clipping
  (`TessellateGridClippedExact`, the Greiner-Hormann polygon clipper) has
  a 3D-solid equivalent of: cutting a closed solid into two closed,
  independently-valid halves along a plane, each auto-capped with a flat
  face at the cut - not two open shells needing a separate capping step.
  Backed directly by Manifold's own `Manifold::SplitByPlane` (already
  depended on for `BooleanCombine()`), not a from-scratch clipper.
  Verified the actual `{first, second}` side convention *by testing*
  rather than assuming it from the one-line doc comment above
  `TrimByPlane` in Manifold's own source: splitting a box down its own
  midplane gives exactly two volume-4 halves whose bounding boxes confirm
  the first result is on the side `plane_normal` points toward, the
  second on the opposite side; an off-center split (a 4x2x2 box at x=3,
  not its midpoint) gives exact volumes 4 and 12, ruling out a test that
  only happens to work for a symmetric split; and both halves' own
  watertightness is proven the same way every other closed-solid
  primitive here is - a real Manifold union with a disjoint cylinder,
  not just "the volume number looked plausible."
- `ConvexHull()` wraps Manifold's own `Manifold::Hull(const
  std::vector<vec3>&)` - a real quickhull-family algorithm, not something
  derived here - to build a closed watertight solid directly from a point
  cloud. Verified with a cube's own 8 corners (hand-derivable exact
  volume 8, watertightness proven the same way every other closed-solid
  primitive here is) and, more meaningfully, with extra points added
  strictly *inside* that hull (the cube's own center, and the center of
  one face): the result's volume doesn't change at all, confirming
  interior points are correctly ignored rather than accidentally
  influencing the hull - the property that makes "hull of everything, no
  pre-filtering needed" actually true rather than just claimed.
- `Simplify()` wraps Manifold's own `Manifold::Simplify`, a real
  quadric-error-style decimation algorithm, to reduce a mesh's triangle
  count while keeping every point within a given tolerance of the
  original surface. Verified with a genuinely dramatic, hand-derivable
  exact case rather than a vague "it got smaller": a box tessellated at
  20x20 per face (4800 redundant coplanar triangles - a bilinear surface
  tessellated finely is still exactly flat, so those triangles carry no
  actual shape information beyond the original 12) simplifies down to
  *exactly* 8 vertices and 12 triangles - the box's own true minimal
  representation - with volume preserved exactly, not approximately,
  since the true surface really was flat and a real decimation algorithm
  should introduce no error there.
- `MinkowskiSum()`/`MinkowskiDifference()` wrap Manifold's own real
  Minkowski-sum/erosion algorithms - growing or shrinking a solid by
  another, useful for rounding corners (summing with a small sphere) or a
  uniform collision/clearance margin, not something derivable from this
  kernel's existing booleans. Verified `MinkowskiSum()` with a
  hand-derivable exact case straight from the definition
  `A+B = {a+b : a in A, b in B}`: two axis-aligned boxes sum to a third
  box whose min/max corners are each input's own corners added
  component-wise (`[0,2]x[0,3]x[0,4] + [0,1]^3 = [0,3]x[0,4]x[0,5]`,
  volume 60 - not the wrong "sum of the two volumes" answer, 25). A
  real, non-obvious discovery while testing the round trip
  `MinkowskiDifference(MinkowskiSum(a, b), b)`: it recovers a box
  congruent to `a` (exactly `a`'s own dimensions and volume) but
  translated by `b`'s own extent, not repositioned back to `a`'s exact
  original location - a genuine property of erosion for a `b` that isn't
  itself centered on the origin, not a limitation of the wrapper, so the
  test asserts the size/volume invariant rather than an unfounded exact
  position.
- `Decompose()` splits a mesh into its disconnected pieces, backed by
  Manifold's own `Manifold::Decompose` - the missing counterpart to
  `Mesh::MergeAndWeld()` concatenating several meshes into one with no
  way to tell the pieces apart again afterward. Verified by merging two
  disjoint (non-touching, non-overlapping) boxes with individually
  hand-known volumes (8 and 6) into a single `Mesh`, then confirming
  `Decompose()` gives back exactly 2 pieces (not merged into one
  connected shape, since they never touch) whose volumes exactly match
  the two originals - matched by volume rather than index, since the
  returned order isn't specified.
- `MinGap()` wraps Manifold's own `Manifold::MinGap`: the two-solid
  counterpart to `Mesh::SignedDistance()` (one mesh, one point) - the
  minimum distance between two whole solids' surfaces, 0 if they overlap
  or touch at all. Verified with three hand-derivable exact cases: two
  boxes separated by exactly 3 units along one axis give a gap of exactly
  3.0; two overlapping boxes give exactly 0.0 (via Manifold's own real
  intersection check, not a coincidentally-small search result); and two
  boxes sharing a boundary face (touching, not overlapping) also give
  exactly 0.0, confirming "touching" isn't treated as some tiny positive
  gap.
- `RefineToLength()` wraps Manifold's own `Manifold::RefineToLength` -
  the opposite direction from `Simplify()` (adding detail rather than
  removing it), subdividing triangles so no edge exceeds a target length
  without changing the underlying shape at all. Verified on the same
  2x2x2 box `Simplify()`'s own test starts from, in the opposite
  direction: a target length (0.5) well below the box's own 2-unit edges
  measurably increases the triangle count, while volume is preserved
  exactly - the shape really is flat everywhere, so subdividing a face
  into more triangles can't change what region it covers.
- `SmoothAndRefine()` turns a faceted polyhedron (e.g. `ConvexHull()`'s
  flat-faced output) into an approximation of a smoothly curved surface,
  combining `Manifold::SmoothOut` and `Manifold::RefineToLength` into one
  function - deliberately, not as two separate wrappers matching the
  other Manifold-backed functions' own one-call-one-wrapper pattern.
  **A real API-design bug was caught before shipping, not just avoided by
  luck**: `SmoothOut` only records half-edge tangent vectors on the live
  Manifold object - the geometry doesn't actually change until a
  subsequent Refine call interpolates new vertices from them - and those
  tangents live only in Manifold's own internal representation, not in
  this kernel's `Mesh`/`ON_Mesh` format. An earlier version of this
  feature split `SmoothOut()` and `RefineToLength()` into two separate
  calls (matching every other Manifold wrapper here); testing it against
  a regular octahedron (`ConvexHull()` of the 6 unit-axis points, exact
  volume 4/3) revealed the smoothed-then-refined result came back with
  the *exact same* volume as the never-smoothed input - the intermediate
  `Mesh` round trip had silently discarded the tangents, so smoothing had
  no effect at all, not a subtle inaccuracy. Fixed by combining both
  Manifold calls into one function that never converts to `Mesh` in
  between. Verified for real this time: smoothing the same octahedron
  with every edge forced smooth (`min_sharp_angle=180`, past its own
  ~109.5-degree dihedral angle, which the default angle would instead
  leave faceted) measurably increases its volume, staying below the
  circumscribing unit sphere's volume (4/3*pi) as a sanity bound - since
  every original vertex is exactly 1 unit from the origin, the smoothed
  surface can bulge between vertices but never past them - and the result
  is still proven watertight via a real Manifold union.
- `Mesh::LoadStl()` closes the "export-only" gap `SaveStl()` itself used
  to flag: reads a plain-text ASCII `.stl` file's `facet`/`vertex` blocks
  back into a `Mesh`, faithful to the format's own "no shared vertex
  list" nature (3 new, unshared vertices per facet, not deduplicated -
  `MergeAndWeld({loaded})` is the way back to a welded mesh, same as any
  other independently-tessellated source). Verified with a real round
  trip: `SaveStl()` a 6-quad box (12 triangle facets), reload it, and
  confirm the loaded mesh has exactly those 12 faces and 36 (3-per-facet)
  vertices, exactly the original volume despite the unshared vertices,
  and welds back down to exactly the box's 8 unique corners. Also checked
  `LoadStl()` fails (rather than silently misinterpreting) a facet with
  fewer than 3 vertices. Doesn't attempt to detect or reject a binary
  `.stl` file - a different format entirely that needs a separate parser.
- `TessellateGridClippedExact()`'s concave path closes the last gap this
  section used to flag: a genuinely pathological many-reflex-vertex trim,
  not just the single-reflex-vertex dart every earlier concave test here
  used. A parametrically-built 6-tooth "comb" (12 reflex vertices,
  tooth/gap widths only a few tessellation cells wide at 32x32 divisions)
  measures its exact hand-derivable area - computed from the same
  `tooth_count`/`tooth_width`/`base_height`/`tooth_top` values that
  generated the vertex list, not eyeballed off hardcoded coordinates, the
  same "trust the formula" approach the annulus test elsewhere in this
  file already uses - and, extruded, is proven a genuine watertight solid
  via a real Manifold union, same rigor every earlier concave-trim test
  here uses. Passed on the first attempt - the concave clipper's earlier
  fixes (entry-only tracing, the grid-line nudge) hold up under this
  harder case too, not just the dart it was originally tested against.
- `CountDegenerateTriangles()` wraps Manifold's own
  `Manifold::NumDegenerateTris` - a diagnostic for a mesh built by some
  process this kernel doesn't fully control (a hand-authored mesh, or one
  loaded from a file), since none of this kernel's own primitives are
  expected to ever produce a degenerate triangle. A real discovery while
  testing it, not just quoted from the doc comment: `NumDegenerateTris`'s
  own doc says the library "attempts to remove all of these," and this
  confirmed it directly - deliberately collapsing one triangle to a
  straight line (moving a shared vertex onto the line between two others
  of the same triangle) still reports 0, because Manifold's own mesh
  construction cleans that straightforward case up before
  `NumDegenerateTris()` is ever asked about it. So a nonzero result means
  a degeneracy the library specifically *couldn't* clean up, not "any
  degeneracy that was ever present in the input" - documented precisely
  as that narrower guarantee rather than the broader one the name alone
  might suggest.
- A real architectural fact about `Brep` surfaced by checking
  `ON_Brep::IsValid()` directly, not assumed: every face-adding factory
  here (`Box()`, `Sphere()`, `TrimmedPlanarFace()`) builds its face via
  `ON_Brep::NewFace(int surface_index)` - the minimal, surface-only
  overload - rather than constructing genuine `ON_Brep` vertex/edge/trim/
  loop topology, so `IsValid()` reports every `Brep` this kernel builds
  as invalid, even a perfectly good one like `Box()`. Confirmed this
  doesn't stop a `Brep` from being fully usable through this kernel's own
  pipeline (`Tessellate()`/`TessellateToClosedMesh()` never call
  `IsValid()` and don't need the topology it checks for) with a real
  test: `Box().raw().IsValid()` is `false`, but the same `Brep` still
  tessellates and welds into an exactly-volume-8 watertight solid.
  Documented directly on the `Brep` class now, since it's the kind of
  fact a future maintainer extending file I/O or Brep construction needs
  to know before assuming `IsValid()` means what it usually would.
- `NurbsSurface::IsClosed()`/`IsPeriodic()` answer a real gap nothing
  here could before: whether a surface wraps seamlessly onto itself in a
  given parameter direction (needed to know before building a
  cylindrical/spherical Brep face by hand, the way `Brep::Sphere()`
  already does). Delegates to `ON_NurbsSurface::IsClosed`/`IsPeriodic`
  after verifying both are real implementations (check the actual knot
  vector and coincident control points, not stubs). Verified with a real,
  informative distinction, not just two boolean checks that happen to
  both work: a flat bilinear surface is open (and non-periodic) in both
  directions, while a real cylinder wall (`ON_Cylinder::GetNurbForm`) is
  closed in U (the circular direction) and open in V (height) - but,
  confirmed by testing rather than assumed, its U closure comes from a
  *clamped* knot vector with coincident end curves, not a genuinely
  periodic one (`IsPeriodic(0)` is `false`), showing the two methods
  really do answer different questions rather than being two names for
  the same fact.
- `NurbsCurve::IsClosed()`/`IsPeriodic()` are the curve-level counterparts
  to `NurbsSurface`'s, same delegation and stub-vs-real verification.
  Verified with the same real distinction: a curve whose control points'
  first and last entries coincide (`FromControlPoints()` always builds a
  clamped, non-periodic knot vector) is closed but not periodic, matching
  what the surface-level test already found for a cylinder wall - a
  genuinely open curve is neither.
- `NurbsCurve::Reverse()` flips a curve's own parameterization direction
  in place (same 3D shape, opposite direction of travel) - a real gap
  nothing here could answer before without discarding the curve and
  rebuilding it from reversed control points, losing any degree
  elevation or other in-place edits already applied. Delegates to
  `ON_NurbsCurve::Reverse` after verifying it's a real implementation.
  Verified `PointAt(t)` after reversing exactly matches the original
  curve's `PointAt(1-t)`, with `TangentAt()` exactly negated at that same
  point - and, a real discovery caught by the test rather than assumed,
  the domain interval's own min/max *values* aren't necessarily preserved
  by `Reverse()` (a `[0, 1]` domain came back as `[-1, 0]` in the verified
  case) - documented on the method now so a caller doesn't reuse a domain
  captured before calling it.
- `NurbsSurface::Reverse(direction)`/`Transpose()` are the surface-level
  counterparts, with the same domain-not-preserved caveat confirmed again
  for `Reverse()`. Both flip the surface's own outward normal - `Reverse()`
  because `u_dir x v_dir` negates when either direction reverses,
  `Transpose()` because swapping U and V gives `v_dir x u_dir =
  -(u_dir x v_dir)` - verified independently for each on the same flat
  `P(u,v)=(u,v,0)` surface `TestSurfaceNormalAt()` established has normal
  exactly `(0,0,1)`: both `Reverse(0)` and `Transpose()` flip it to
  exactly `(0,0,-1)`, checked separately rather than assuming they behave
  the same way just because both involve "reversing something".
- `NurbsCurve::Trim()` shortens a curve in place to a sub-domain,
  delegating to `ON_NurbsCurve::Trim` (a genuine de Boor knot-insertion
  algorithm, verified as real rather than assumed) after checking `t0 <
  t1`. A real gap nothing here could answer before: cutting a curve down
  to part of itself previously meant re-sampling points and building a
  brand new approximating curve, not keeping the exact same underlying
  curve restricted to a smaller range. Verified on a straight line from
  `(0,0,0)` to `(10,0,0)` (no curvature for a knot-insertion trim to
  approximate away, so every check is hand-derivable exact): trimming to
  `[0.2, 0.7]` gives a curve whose own domain is exactly that interval
  (confirmed by testing, not assumed to renormalize back to `[0,1]`),
  whose endpoints are exactly `(2,0,0)` and `(7,0,0)`, and whose own
  `Length()` is exactly `5.0`, not the original `10.0`. Also checked
  `Trim()` fails outright on a backwards interval (`t0 >= t1`) rather
  than doing something undefined.
- `NurbsCurve::Split()` is `Trim()`'s complement: instead of keeping one
  sub-range and discarding the rest, it keeps both halves as two
  independent curves. Delegates to `ON_NurbsCurve::Split` (the same
  underlying knot-insertion algorithm `Trim()` uses, verified real)
  through its old-style `ON_Curve*&` output-parameter API, casting back
  to `ON_NurbsCurve` and copying into the two output curves. Verified on
  the same line `TestCurveTrim()` uses: splitting at `t=0.4` gives a left
  half running exactly `(0,0,0)` to `(4,0,0)` over domain `[0, 0.4]` and a
  right half running exactly `(4,0,0)` to `(10,0,0)` over `[0.4, 1]` - the
  two halves share the exact same split point (no gap or overlap) and
  their lengths sum back to exactly the original line's own length. Also
  checked `Split()` fails when `t` sits at either domain endpoint rather
  than strictly inside it.
- `NurbsSurface::Trim(direction, t0, t1)` is the surface-level counterpart
  to `NurbsCurve::Trim()`: shortens the surface in place to `[t0, t1]` in
  just one direction (0 = U, 1 = V), leaving the other direction's domain
  untouched. Delegates to `ON_NurbsSurface::Trim`. Verified on the same
  flat `P(u,v)=(u,v,0)` surface `TestSurfaceReverseAndTranspose()` uses:
  trimming direction 0 to `[0.2, 0.7]` gives that direction's own new
  domain exactly `[0.2, 0.7]` while direction 1's domain stays exactly
  `[0, 1]`, and evaluating the trimmed domain's own corners lands exactly
  on `(0.2, 0, 0)` and `(0.7, 1, 0)` - confirmed by a debug run before
  finalizing the assertions, not assumed from the curve-level behavior.
  Also checked `Trim()` fails on a backwards interval (`t0 >= t1`) rather
  than silently doing something undefined.
- `NurbsSurface::Split(direction, t, out_west_or_south, out_east_or_north)`
  is `Trim()`'s complement, mirroring `NurbsCurve::Split()`: keeps both
  halves as two independent surfaces instead of discarding one. Delegates
  to `ON_Surface::Split` through its old-style `ON_Surface*&`
  output-parameter API, casting back to `ON_NurbsSurface`. Verified on the
  same flat `P(u,v)=(u,v,0)` surface: splitting direction 0 at `t=0.4`
  gives a west half with `domain(0)` exactly `[0, 0.4]` and an east half
  with `domain(0)` exactly `[0.4, 1]`, both keeping direction 1's domain
  `[0, 1]` unchanged, and the west half's `u_max` edge and the east half's
  `u_min` edge land at the exact same point `(0.4, 0, 0)` - no gap or
  overlap at the split line, confirmed by a debug run before finalizing
  the assertions. Also checked `Split()` fails when `t` sits exactly at a
  domain endpoint rather than strictly inside it.
- `Mesh::LoadStl()` now reads binary `.stl` files too, closing the gap
  this section used to flag - it previously only handled ASCII. Detects
  which of the two genuinely different formats a file actually is by its
  exact size rather than by sniffing for the text `solid` (a binary
  file's own 80-byte header can start with those bytes too, per the
  spec, so that keyword alone was never a reliable discriminator): a
  binary STL's total size is always exactly `80 + 4 + count*50` bytes for
  the triangle count its own header claims, so a file matching that
  formula is parsed as binary; anything else falls back to the existing
  ASCII parser. Verified against a minimal 2-triangle binary STL file
  written byte-for-byte by hand (not round-tripped through this kernel's
  own writer on both ends, since `SaveStl()` only ever writes ASCII): the
  loaded mesh has exactly the 2 written triangles, 6 unshared vertices
  (STL's own "no shared vertex list" structure, same as the ASCII path),
  and an area of exactly 1.0 matching the hand-written unit-square
  triangles. Also discovered (by testing, not assumed) what actually
  happens to a file whose 84-byte binary-looking header claims a
  triangle count that doesn't match its real remaining size: it fails
  the binary size check, falls through to the ASCII parser, which finds
  no recognizable ASCII tokens in the raw header bytes at all and
  returns an empty mesh with `Result::Ok` rather than `Result::Failed` -
  a narrower, now-documented real guarantee rather than an assumed
  outright failure.
- `NurbsCurve::Extend(t0, t1)` and `NurbsSurface::Extend(direction, t0,
  t1)` are `Trim()`'s opposite: instead of cutting a curve/surface down,
  they analytically extrapolate it outward to include `[t0, t1]`,
  delegating to `ON_NurbsCurve::Extend`/`ON_NurbsSurface::Extend` (the
  surface version converts the given direction to an isocurve, extends
  that via the same curve-level algorithm, and writes it back - the same
  "real, not a stub" pattern `Trim()`/`Split()` already established for
  both). A request already contained within the current domain is
  intercepted before ever calling into OpenNURBS and reported as
  `NoOpAlreadySatisfied`, since OpenNURBS' own `Extend` would otherwise
  return the same `false` for that case as for a genuine failure (e.g. a
  closed curve/surface), making the two indistinguishable from its
  return value alone. Verified on the same straight-line curve and flat
  `P(u,v)=(u,v,0)` surface used elsewhere: extending a `[0,1]`-domain
  line to `[-0.5, 1.5]` gives new endpoints at exactly `(-5,0,0)` and
  `(15,0,0)` - the same `P(t)=(10t,0,0)` equation extrapolated, not a
  different curve - and extending the surface's direction 0 to `[-0.5,
  1.0]` leaves direction 1's domain untouched and evaluates its new edge
  at exactly `(-0.5, 0.5, 0)`. Also discovered, by testing rather than
  just reading OpenNURBS' own source: `ON_NurbsCurve::IsClosed()`
  unconditionally requires at least 4 control points before it even
  looks at endpoint positions, so a 3-point coincident-endpoint polyline
  (tried first) reports `IsClosed()` false and doesn't exercise the
  "fails on a closed curve" case at all - a 4-point closed triangle path
  was needed instead, and `NurbsCurve::IsClosed()`'s own doc comment has
  been corrected to note this real, narrower guarantee.
- `Mesh::SaveStlBinary()` closes the last STL asymmetry: `SaveStl()`
  only ever wrote ASCII, even though `LoadStl()` (see above) already
  reads both. Same triangle-only, no-shared-vertex-list, real-computed
  -normal semantics as `SaveStl()`, just the binary encoding (80-byte
  zeroed header, little-endian uint32 triangle count, then 50-byte
  normal/vertices/attribute-count records per triangle). Verified two
  ways on the same 6-quad box `SaveStl()`'s own round-trip test uses:
  the written file's exact byte size independently matches
  `80 + 4 + 12*50` for its 12 triangles (checked directly, not just
  inferred from a successful round-trip - confirms the real binary
  layout was written, not merely something `LoadStl()` happens to
  accept), and reading that file back through `LoadStl()`'s own
  binary/ASCII auto-detection reproduces the original mesh's 12 faces,
  36 unshared vertices, and exact volume - the first time that
  auto-detection has been exercised against a real (not hand-written)
  binary file.
- `Mesh::SetTextureCoordinates()`/`HasTextureCoordinates()`/
  `TextureCoordinateAt()` add real per-vertex (u, v) texture coordinate
  storage, backed by `ON_Mesh::m_S` - not the deprecated `m_T` OpenNURBS'
  own header explicitly flags as superseded by `m_S` (confirmed by
  reading `opennurbs_mesh.h`, not assumed). Same per-vertex-only
  granularity as every other piece of data here (one position, one
  computed normal per vertex) - there's no per-face-corner UV storage, so
  a genuine UV seam can't be represented. `SaveObj()`/`LoadObj()` are
  wired to this: `SaveObj()` writes a `vt` line per vertex and switches
  face lines to `v/vt/vn` form whenever `HasTextureCoordinates()` is
  true (unchanged `v//vn` form otherwise); `LoadObj()` reads `vt` lines
  and any `v/vt`/`v/vt/vn` face references, storing each corner's texture
  coordinate against that corner's *vertex*. Verified on the same 8
  -vertex box `SaveObj()`'s own test uses: a full round trip through
  `SaveObj()`/`LoadObj()` reproduces every vertex's exact texture
  coordinate. Also verified the deliberately-chosen behavior for a file
  where only some vertices are ever referenced with a `vt` (a legitimate
  but partial-coverage `.obj`, which `ON_Mesh`'s own "every vertex or
  none" convention for `m_S` can't represent): the reloaded mesh reports
  no texture coordinates at all rather than guessing values for the
  unreferenced vertices.
- `NurbsCurve::ClosestPoint()`/`ClosestPointParameter()` and
  `NurbsSurface::ClosestPoint()`/`ClosestPointParameter()` close a gap
  `Mesh` already had (`Mesh::ClosestPoint()`) but curves/surfaces didn't:
  finding the point on the curve/surface nearest to an arbitrary query
  point. Both are from-scratch numeric searches, not wrappers - verified
  directly against the v8.34 source that OpenNURBS' public `ON_Curve`/
  `ON_Surface` API has no `GetClosestPoint()` method at all (grepped the
  whole source tree), the same "declared for Rhino, not present in the
  public build" gap this file already found for `Length()`'s own
  arc-length method. The curve version coarsely samples the domain, then
  refines the bracket around the best sample via golden-section search;
  the surface version does the 2D analog via repeated grid refinement
  (sample a grid, shrink the search region around the best cell, repeat).
  Neither guarantees a true global minimum for a pathological
  multi-modal distance function - same "approximate, not exhaustive"
  honesty `Length()`'s own polyline sampling already documents. Verified
  on hand-derivable-exact cases: a straight line's closest point to an
  off-line query point matches its exact perpendicular projection (to
  within the search's own convergence tolerance, ~4e-8 for the curve's
  golden-section refinement), a flat plane's closest point to a point
  above it matches its exact vertical projection (~4e-5 for the
  surface's coarser grid refinement), and a query point far outside a
  surface's domain correctly clamps to the domain's own boundary corner
  rather than extrapolating past it.
- `NurbsCurve::CurvatureAt(t)` returns the curve's curvature vector
  (magnitude `1/R`, pointing toward the local center of curvature; zero
  where the curve is locally straight). Delegates to `ON_Curve::
  CurvatureAt` after verifying it's a real implementation (calls through
  to `EvCurvature`/`Ev2Der`, an actual second-derivative computation, not
  a stub - same standing discipline this file already applies to
  `Length()`/`TangentAt()`). Verified against a NURBS circle of known
  center and radius built via `ON_Circle::GetNurbForm` (the same
  construction the cylinder-wall test already relies on): at every
  parameter tested, the curvature vector's magnitude is exactly `1/R`,
  and recovering the osculating circle's center from it
  (`point + curvature_vector / curvature_vector.LengthSquared()`, the
  standard way to go from a nonzero curvature vector back to its circle's
  center) lands exactly on the known center. Also checked a straight
  line's curvature is exactly zero.
- `NurbsSurface::CurvatureAt(u, v)` returns a new `SurfaceCurvature`
  (Gaussian, mean, and both principal curvatures) at a point - the
  surface-level counterpart to `NurbsCurve::CurvatureAt()`. A
  from-scratch computation, not a wrapper: verified directly against the
  v8.34 source that OpenNURBS' `ON_SurfaceCurvature` is just a plain data
  holder (a "Create" factory from already-known curvature values,
  comparison operators), not something that computes curvature from a
  surface's own derivatives - the same "declared for Rhino data
  interchange, not a public computation" pattern this file already found
  for `ON_Brep::CreateMesh`. Computed from `ON_Surface::Ev2Der` (verified
  real) via the classical first/second fundamental form formulas.
  Verified on hand-derivable-exact cases: a flat plane's curvature is
  exactly zero everywhere, and a sphere of known radius (via
  `ON_Sphere::GetNurbForm`, the same construction `Brep::Sphere()` uses)
  has Gaussian curvature exactly `1/radius^2` at every point tested
  (sign-unambiguous, since Gaussian curvature is a product of two
  curvatures under the same sign convention) and is an umbilic
  everywhere (k1 == k2). The *sign* of mean/principal curvature was not
  assumed - a debug run first showed this surface's outward-pointing
  normal gives every sphere point a *negative* mean curvature
  (`-1/radius`, curving away from the outward normal toward the
  interior), and that's the behavior now documented and tested, not a
  guessed convention.
- `NurbsCurve::SuggestedSamples(chord_tolerance)` is a first, modest step
  toward this section's own long-flagged "adaptive/curvature-aware
  meshing... unstarted" gap (see below) - not the full feature, but a
  genuinely curvature-informed number a caller can now get instead of
  guessing a sample count for `Length()` or similar polyline samplers.
  Samples `CurvatureAt()` (see above) across the domain, takes the
  tightest radius of curvature found, and applies the standard circular
  -arc chord-height (sagitta) formula assuming the whole curve turns at
  that tightest radius - a deliberately conservative estimate for a
  curve whose curvature actually varies, but exact for one whose
  curvature is genuinely constant. Verified on a full circle of known
  radius (the one case where "assume constant curvature" is exactly
  true, not just a safe approximation): the suggested count exactly
  matches an independently hand-computed application of the same
  chord-height formula, using the circle's own exact total turning angle
  (`2*pi`) rather than this method's `Length()/radius` approximation of
  it - which for a full circle is itself exact, since `Length()`
  converges to the true circumference. Also checked a straight line
  (zero curvature everywhere) always returns exactly 1, and that a
  non-positive `chord_tolerance` throws `std::invalid_argument`.
- `NurbsSurface::SuggestedDivisions(chord_tolerance)` is the
  surface-level counterpart to `NurbsCurve::SuggestedSamples()` - a
  suggested u/v division count for `TessellateGrid()`/
  `TessellateGridClippedExact()`, still one number per direction for the
  whole surface (not a per-region adaptive tessellator), but now a real
  building block toward the "adaptive/curvature-aware meshing" gap
  rather than nothing at all. Computes each direction independently via
  `ON_Surface::IsoCurve` (verified real - builds a genuine `ON_NurbsCurve`
  by slicing the control net, not a stub): samples several isocurves
  running the *other* direction, wraps each as a `NurbsCurve`, and takes
  the worst-case `SuggestedSamples()` result found. Verified on the same
  cylinder wall `TestSurfaceIsClosed()` uses (U the circular direction,
  V the straight height direction): the U division count exactly matches
  an independently hand-computed chord-height formula for the wall's
  unit-radius circular cross-section (every U-isocurve is that same
  exact circle regardless of V), and the V division count is exactly 1
  (every V-isocurve is a straight line with zero curvature). Also
  checked a non-positive `chord_tolerance` throws
  `std::invalid_argument`.
- `NurbsSurface::TessellateGridAdaptive(chord_tolerance)` closes the loop
  on the last few chunks' curvature-based building blocks: a one-call
  path that picks `u_divisions`/`v_divisions` via `SuggestedDivisions()`
  and tessellates via `TessellateGrid()`, instead of a caller having to
  know to call `SuggestedDivisions()` itself first. A thin, deterministic
  composition of two already-verified pieces, so its own test just
  confirms the wiring: the same cylinder wall tessellated through
  `TessellateGridAdaptive()` produces exactly the same face count,
  vertex count, and area as calling `SuggestedDivisions()` then
  `TessellateGrid()` by hand.
- `NurbsSurface::TessellateGridClippedExactAdaptive(chord_tolerance,
  trim_polygon)` is `TessellateGridAdaptive()`'s exact-clip counterpart,
  same "one call instead of two" convenience over
  `SuggestedDivisions()` + `TessellateGridClippedExact()`. Verified on
  the same 10x10 flat surface and `[0.15,0.85]^2` trim
  `TestExactClippingMatchesAreaButNotCellCounts` uses: the resulting
  mesh's area is exactly 49 (the true trim area, independent of grid
  resolution since exact clipping measures the real boundary rather than
  approximating it), and matches the manual `SuggestedDivisions()` +
  `TessellateGridClippedExact()` two-call equivalent exactly in vertex
  and face count.
- `Brep::TessellateAdaptive(chord_tolerance)`/
  `TessellateToClosedMeshAdaptive(chord_tolerance)` are the Brep-level
  endpoint of this run of curvature-based tessellation chunks: each
  face gets its own `NurbsSurface::SuggestedDivisions()`-derived
  resolution instead of one fixed division count shared across every
  face, so a flat face and a tightly curved face in the same Brep get
  independently appropriate detail. Verified two ways: `Box()` (every
  face flat) gets exactly the minimum 1x1 division per face at any
  tolerance tested (12 triangles total across 6 faces) with an exactly
  correct closed volume of 8.0 for a 2x2x2 box; `Sphere()` (real
  curvature everywhere) shows genuine adaptation working, not just
  running without crashing - a loose 0.5 tolerance gave 36 faces and a
  volume far from the true analytic `4/3 * pi * r^3`, while a tight 0.01
  tolerance gave over 10x as many faces and landed within 2% of the true
  volume.
- `SubD::EdgeCount()` adds the third topology count alongside the
  existing `FaceCount()`/`VertexCount()` - a small gap (no way to get an
  edge count at all before) closed via `ON_SubD::EdgeCount`. Verified
  against a 6-quad box control mesh: exactly the cube's own known
  topology at level 0 (V=8, E=12, F=6), and after 2 global Catmull-Clark
  subdivisions, exactly the hand-derived counts (V=98, E=192, F=96) -
  with Euler's formula `V - E + F = 2` checked directly against
  `EdgeCount()`'s own reported value, confirming it's real edge
  topology, not some other count that happened to look plausible.
- `SubD::CreaseEdgeCount()` counts the current level's own crease edges -
  a way to directly check how many creases exist, rather than only
  their geometric effect (the fold-stays-straight check
  `TestSubDCreaseAtDoubleEdgeKeepsFoldStraight()` already had). NOT a
  wrapper around `ON_SubD::CreaseEdgeCount` despite that method existing
  in OpenNURBS' own public header - verified by grepping the whole
  v8.34 source tree that it's declared but never actually implemented
  anywhere (only an unrelated class, `ON_SubDVertexSharpnessCalculator::
  CreaseEdgeCount`, exists) - the same "declared for Rhino, not present
  in the public build" pattern this file's own class comment already
  flags for `ON_SubD::BrepForm`. Implemented instead via the real,
  working `ON_SubD::EdgeIterator()` + `ON_SubDEdge::IsCrease()` (the
  exact pattern `ON_SubD::FirstEdge()`'s own doc comment recommends).
  Verified on the existing hinge test's own two-quad mesh, and along the
  way discovered (by testing, not assumed) a real OpenNURBS convention:
  an open SubD's own boundary edges are themselves always creases,
  independent of `crease_at_double_edges` - the hinge's 6 outer boundary
  edges are creased either way; that flag only changes whether the one
  interior fold edge is *also* a crease (6 total without it, all 7 with
  it, out of 7 total edges).
- `NurbsSurface::IsPlanar(tolerance)` delegates to `ON_NurbsSurface::
  IsPlanar` (verified real - fits a plane through the surface's own
  evaluated normal at its domain center, then checks every control
  point's distance to that plane; genuinely sound, not just a heuristic,
  since a non-rational NURBS surface always lies within its own control
  points' convex hull). Verified against the same doubly-curved bicubic
  bulge surface `TestBrepGetTightBoundingBoxOvershootsInteriorExtremum`
  uses, and along the way found a real, non-obvious threshold rather
  than assuming one: the fitted plane passes through the surface's own
  *evaluated* center point (`0.25*peak_height`, not `0`), so the actual
  planar/non-planar threshold is `0.75*peak_height` (the peak control
  point's distance to that plane), not the naively-guessable
  `peak_height` itself - confirmed by a debug run straddling that exact
  value before finalizing the test.
- `NurbsCurve::IsPlanar(tolerance)` is the curve-level counterpart,
  delegating to `ON_NurbsCurve::IsPlanar` (verified real - checks
  `IsLinear()` first, then fits/verifies an actual plane through the
  curve's own tangent and sampled points, not a stub). Verified with a
  curve whose control points all share one coordinate plane (planar), a
  genuinely non-coplanar 4-point curve (non-planar at a tight tolerance,
  planar once the tolerance is generous enough to swallow its actual
  deviation), and a straight line (trivially planar, confirmed directly
  rather than assumed).
- `NurbsCurve::IsLinear(tolerance)` - a stronger condition than
  `IsPlanar()` (every linear curve is planar, but not every planar curve
  is linear, e.g. an arc) - delegates to `ON_Curve::IsLinear`, the same
  method `IsPlanar()`'s own real implementation already relies on
  internally. Verified with a straight line (trivially linear) and the
  same quadratic bulge curve `TestCurveGetTightBoundingBox` uses: reports
  non-linear at a tight tolerance, linear once the tolerance is generous
  enough to swallow its actual deviation from the endpoint-to-endpoint
  line.
- `NurbsSurface::IsSphere(tolerance)` classifies whether a surface is (a
  portion of) a sphere. `ON_NurbsSurface` doesn't override
  `ON_Surface::IsSphere`, so this inherits the base class's own real
  implementation (verified by reading opennurbs_revsurface.cpp, not
  assumed): takes two isocurves through the domain's midlines, checks
  each is genuinely a circular arc, then verifies both arcs' fitted
  spheres agree with each other and with sampled points elsewhere on the
  surface - a real geometric classification, not a name-based guess.
  Verified against `ON_Sphere::GetNurbForm`'s own construction (the same
  one `Brep::Sphere()` uses, reports true) and, importantly, against
  shapes that must correctly report false rather than "anything curved
  is a sphere": a flat surface and a cylinder wall (curved in only one
  direction).
- `NurbsSurface::IsCylinder(tolerance)` is `IsSphere()`'s sibling
  classification, same inheritance situation (`ON_NurbsSurface` doesn't
  override `ON_Surface::IsCylinder`, so it's the base class's own real
  implementation - one isocurve direction must be a circular arc and the
  other a straight line, consistent along the line). The two methods'
  tests are literally each other's negative case: the cylinder wall that
  correctly reports `IsSphere()` false now correctly reports
  `IsCylinder()` true, and the sphere that reports `IsSphere()` true
  correctly reports `IsCylinder()` false - together showing these are
  real, mutually-distinguishing classifications, not "anything curved
  reports true for everything".
- `NurbsSurface::IsCone(tolerance)` completes the quadric-classification
  trio alongside `IsSphere()`/`IsCylinder()`, same inheritance situation
  (`ON_NurbsSurface` inherits `ON_Surface::IsCone`'s real base
  implementation) - structurally almost identical to `IsCylinder()`'s
  check (one isocurve a circular arc, the other a straight line), but a
  cone's line isocurves converge to a single apex rather than staying
  parallel, which is the actual distinguishing test. Verified against a
  genuine cone (`ON_Cone::GetNurbForm`, reports true) and against the
  existing cylinder wall and sphere (both correctly report false - a
  cylinder's parallel isocurves never converge, and a sphere has no
  straight-line isocurve in either direction at all).
- `NurbsSurface::IsTorus(tolerance)` is the fourth and last of this
  quadric-classification family, same inheritance situation
  (`ON_NurbsSurface` inherits `ON_Surface::IsTorus`'s real base
  implementation) - both isocurve directions must be circular arcs whose
  fitted tori agree with each other, unlike `IsCone()`/`IsCylinder()`'s
  one-arc-one-line requirement. A real discovery here, not assumed: a
  genuine torus via `ON_Torus::GetNurbForm` reports `IsTorus()` *false*
  at the default tolerance (`ON_ZERO_TOLERANCE`, ~2.3e-10) -
  `GetNurbForm`'s own rational biquadratic construction has
  floating-point round-off just outside that extremely tight bound -
  but reports true at a still-tight 1e-6 tolerance. This isn't a bug in
  `IsTorus()` itself or the whole classification family (the sphere/
  cylinder/cone cases all passed at the default tolerance in their own
  tests), just the real precision this specific rational NURBS
  construction needs. Verified against the existing sphere, cylinder
  wall, and cone too (all correctly report false at the same 1e-6
  tolerance).
- `NurbsCurve::IsArc(tolerance)`/`IsCircle(tolerance)` are the
  curve-level classification counterparts to the surface family above.
  `IsArc()` delegates to `ON_Curve::IsArc` (verified real - fits an
  actual plane and circle through sampled points; `ON_NurbsCurve::IsArc`
  falls back to this same base method for the general case). `IsCircle()`
  is the stronger "and its own angle is exactly 2*pi" check
  (`ON_Arc::IsCircle()`), built on the same `IsArc()` fit. Verified with
  a genuine full circle (both true), a quarter arc of the *identical*
  circle (`IsArc()` true, `IsCircle()` false - the real case proving
  `IsCircle()` isn't just `IsArc()` renamed), and a straight line (both
  false).
- `NurbsSurface::GetApproximateSize()` returns an approximate
  width/height for a surface's own domain, delegating to
  `ON_NurbsSurface::GetSurfaceSize` after verifying it's real but
  genuinely approximate by its own source comment (`// TODO - get
  lengths of polygon`): each direction is the *control polygon length*
  (straight-line distances between consecutive control points), not the
  true arc length of an isocurve - exact for a flat/straight direction,
  an overstatement for a curved one, the same direction of error
  `NurbsCurve::Length()`'s own polyline sampling has, just from one
  coarse pass rather than a convergent fine one. Verified exactly on a
  flat surface (width/height match its true 3x2 size) and confirmed to
  meaningfully overstate a cylinder wall's true circumference (8.0 vs.
  the true `2*pi ~ 6.28`) rather than merely rounding-error off it.
- `NurbsCurve::SuggestedParameterValues(chord_tolerance)` is a further,
  more substantial step toward the flagged "adaptive/curvature-aware
  meshing" gap than `SuggestedSamples()`'s single global count: it
  returns a genuinely non-uniform, per-region-adaptive set of parameter
  values (denser where the curve actually bends more), via the standard
  recursive chord-height (flatness) bisection technique used by real
  curve flatteners. Verified on a straight line (needs no bisection at
  all - exactly `[Domain().Min(), Domain().Max()]`, 2 values) and a
  full circle (constant curvature): the recursive bisection - which
  always splits a segment exactly in half - naturally lands on the
  smallest power-of-2 segment count at or above `SuggestedSamples()`'s
  own independently-computed minimum threshold (64 vs. 50 here),
  confirmed to match exactly, and the resulting breakpoints are
  genuinely uniformly spaced, the correct degenerate case when there's
  nothing to actually adapt to.
- `NurbsSurface::TessellateGridNonUniform(u_values, v_values, ...)`
  generalizes `TessellateGrid()` to an explicit, not-necessarily-uniform
  set of parameter values in each direction instead of an even division
  count - the genuine per-region-adaptive tessellation *primitive* this
  kernel's own flagged "adaptive/curvature-aware meshing" gap was
  missing (a future caller, e.g. one built on
  `NurbsCurve::SuggestedParameterValues()`, can now pass denser
  breakpoints only where a surface actually needs them, while the
  result stays a crack-free complete tensor-product grid - every row
  shares the same `u_values`, every column the same `v_values`, so
  there's no T-junction to create in the first place). `TessellateGrid()`
  itself is now implemented as the special case where both arrays happen
  to be evenly spaced, sharing the same underlying grid-building code -
  verified to produce identical vertex/face counts and area to calling
  `TessellateGrid()` directly with the equivalent evenly-spaced arrays.
  Also verified on a genuinely non-uniform grid (breakpoints deliberately
  clustered near the domain edges) that the flat unit-square surface's
  measured area is still exactly 1.0 regardless of where the (uneven)
  grid lines fall, and that a non-2-entry or non-increasing values array
  throws `std::invalid_argument`.
- `NurbsSurface::SuggestedParameterValues(direction, chord_tolerance)`
  and `TessellateGridNonUniformAdaptive(chord_tolerance)` complete the
  surface-level genuine per-region adaptivity story: the former mirrors
  `SuggestedDivisions()`'s isocurve-sampling structure but keeps whichever
  sampled isocurve's own `NurbsCurve::SuggestedParameterValues()` result
  is richest (same "worst case wins" philosophy, but now returning real
  non-uniform breakpoints instead of one count); the latter is the
  one-call path combining both directions with `TessellateGridNonUniform()`.
  Verified on the same cylinder wall used throughout: direction 0 (the
  circular, genuinely curved direction) needs real bisection, landing on
  the same "smallest power of 2 at or above `SuggestedDivisions()`'s
  threshold" relationship the curve-level test already established (32
  vs. 23 here); direction 1 (the straight height, zero curvature) needs
  none at all, landing on exactly its domain's own 2 endpoints, matching
  `SuggestedDivisions()`'s own `v=1`. Also verified the one-call adaptive
  path produces byte-identical vertex/face counts to calling both
  `SuggestedParameterValues()` calls and `TessellateGridNonUniform()` by
  hand.
- `Brep::TessellateNonUniformAdaptive()`/
  `TessellateToClosedMeshNonUniformAdaptive()` are the Brep-level
  endpoint of the non-uniform per-region story, mirroring
  `TessellateAdaptive()`'s own structure but using `NurbsSurface::
  TessellateGridNonUniformAdaptive()` per face. A face built with
  `exact_clip=true` has no non-uniform exact-clip tessellator yet, so it
  still falls back to the uniform `TessellateGridClippedExactAdaptive()`
  - a real, documented narrower scope, not silently ignored. Verified
  `Box()` gets the identical exact result as the uniform adaptive path
  (12 triangles, volume 8.0). Also discovered a genuine, worth-noting
  nuance testing `Sphere()`: because a sphere's curvature is *isotropic*
  (the same in every direction), the non-uniform path's power-of-2
  dyadic segment counts are actually *less* efficient here than the
  uniform path's directly-computed count (4096 faces vs. the uniform
  adaptive test's own 1520, at the same tolerance) - non-uniform
  adaptivity's real advantage is for a face whose curvature genuinely
  *varies* across it (the cylinder-wall case above), not one that's the
  same everywhere. Both still hit the same real 2%-of-true-volume
  accuracy target.
- `NurbsCurve::ParameterAtArcLength(target_length)` is `Length()`'s
  inverse: finds the parameter at which the curve has traveled a given
  arc length from its own domain start, by walking the same polyline
  `Length()` itself builds and linearly interpolating within whichever
  segment contains `target_length` - the standard approach when only a
  polyline approximation of arc length is available (confirmed, not
  assumed, that OpenNURBS' public build has no closed-form arc-length
  function to invert instead). Clamps to the domain's own start/end for
  an out-of-range `target_length` rather than extrapolating past the
  curve. Verified exact on a 3-4-5 line (half its arc length lands
  exactly at its own midpoint) and exact on a full circle of known
  radius (a quarter of its own arc length lands exactly on its
  geometric quarter point).
- `NurbsCurve::DivideByCount(count)` builds directly on
  `ParameterAtArcLength()`: it calls that function `count - 1` times, at
  `i/count` fractions of the curve's total `Length()`, to produce the
  `count + 1` parameter values (including both domain endpoints) that
  split the curve into `count` sub-segments of exactly equal arc length -
  genuine arc-length-parametrization division, not the naive "divide the
  raw parameter domain evenly" approach that only agrees with it when the
  curve happens to already be arc-length-linear in its own parameter.
  Verified exact on a straight line (uniform speed makes the two
  approaches coincide, confirmed rather than assumed) and, more
  meaningfully, on a full circle: every consecutive pair of division
  points was checked to be the *same actual chord length* apart (not just
  evenly spaced in parameter), confirming true equal-arc-length division
  rather than an artifact of the circle's own parametrization. Throws
  `std::invalid_argument` on a non-positive `count`.
- `NurbsCurve::Domain()` and `NurbsSurface::Domain(direction)`: a real,
  previously-missing gap this file's own doc comments had been quietly
  assuming away - `PointAt()`, `DivideByCount()`,
  `SuggestedParameterValues()`, and others all reference "the curve's own
  domain" or `Domain().Min()`/`Domain().Max()` in their documentation, but
  there was no actual public way for a caller to *ask* a `NurbsCurve` or
  `NurbsSurface` what its own parameter domain is - only `raw().Domain()`
  reaching straight into the underlying `ON_NurbsCurve`/`ON_NurbsSurface`,
  bypassing the wrapper entirely. Both new methods are a thin wrap
  returning a new plain `Interval{min, max}` struct (`types.h`, alongside
  `BoundingBox`, for the same "two independent numbers a caller may want
  separately" reason). Verified to match `raw().Domain()` exactly and, on
  a concrete degree-3/4-control-point curve and a 4x4-control-point,
  degree-3x3 surface, confirmed by a debug run (not assumed from the
  general "clamped uniform knots give a `[0, cv_count - degree]` domain"
  rule) to be exactly `[0, 1]` in every direction.
- `NurbsSurface::CVCountU()`/`CVCountV()`: the surface-level counterpart
  to `NurbsCurve::ControlPointCount()`, another real gap alongside
  `Domain()` above - `DegreeU()`/`DegreeV()` already existed, but nothing
  exposed the control grid's own dimensions. Verified on a deliberately
  non-square (5 x 3 control points, degree 2 x 1) surface, so U and V
  weren't accidentally checked against the same coincidental number, and
  matched exactly against the underlying `ON_NurbsSurface::CVCount(dir)`.
- `NurbsCurve::IsRational()`/`NurbsSurface::IsRational()`: whether the
  object's control points carry genuinely non-uniform weights, as opposed
  to `FromControlPoints()`/`FromControlGrid()`'s own always-non-rational
  construction (`Create()` is always called with `is_rational=false`
  there). Verified both directions with a debug run before finalizing:
  a `FromControlPoints()` line and a `FromControlGrid()` flat surface both
  report `false`, while a genuine circle's and sphere's `GetNurbForm()`
  output (needing real non-uniform weights to trace a true circular arc
  or sphere with a NURBS parametrization) both report `true`.
- `NurbsCurve::WeightAt(i)`/`NurbsSurface::WeightAt(i, j)`: the actual
  per-control-point homogeneous weight, following naturally from
  `IsRational()` above. Reading OpenNURBS' own source
  (`ON_NurbsCurve::Weight`/`ON_NurbsSurface::Weight`) turned up a real,
  worth-documenting asymmetry: on a non-rational object the call
  short-circuits to a hardcoded `1.0` *without ever indexing* `i`/`j` at
  all, so an out-of-range index is harmless there - but on a rational one
  it indexes the raw control-vertex array with no bounds check, a genuine
  unchecked out-of-bounds read for a bad index. Verified with real
  numbers, not just the non-rational/rational booleans: a genuine
  circle's 9-control-point NURBS form has weights that alternate exactly
  `1.0` and `sqrt(2)/2` (the standard rational-quadratic circle
  construction), and a sphere's 9x5 grid's weights are exactly the tensor
  product of that same pattern in each direction independently -
  confirmed by a debug run printing the full grid, not assumed from the
  circle result alone.
- `NurbsSurface::ApproximateArea(u_divisions, v_divisions)`: sums the
  triangle areas of a `TessellateGrid()` tessellation (`Mesh::Area()`)
  rather than numerically integrating the first fundamental form from
  scratch, since the tessellator already exists. This is the mirror image
  of `GetApproximateSize()`'s own error direction: a flat-triangle
  approximation of a curved surface *understates* the true area (unlike
  the control-polygon approach's *overstating*), converging to it from
  below as the grid refines - verified as a real inequality between a
  coarse and fine sphere tessellation (not just "roughly 4*pi*r^2"), with
  the finer grid strictly closer to the true value, and exact at any
  resolution on a flat surface (no curvature to fall short of). Also
  turned up that `TessellateGrid()` itself had no division-count
  validation at all - a `0` division count there used to silently
  produce `NaN` parameter values via an unguarded `0/0`, confirmed by
  reading its source rather than assumed. Fixed directly in
  `TessellateGrid()` itself (now throws `std::invalid_argument` below 1,
  matching `TessellateGridNonUniform()`'s own validation style) rather
  than only working around it in `ApproximateArea()`, so every other
  `TessellateGrid()` caller gets the same protection.
- The same real gap, found by inspection once `TessellateGrid()`'s own
  fix made it worth checking siblings for: `TessellateGridClippedExact()`
  reaches `ParameterAt()` (and, on its concave path, an unguarded grid-
  width division) via `u_divisions`/`v_divisions` the exact same way
  `TessellateGrid()` used to, so a `0` or negative division count there
  had the identical silent-`NaN` failure mode. Fixed the same way -
  throws `std::invalid_argument` below 1, verified with a dedicated test
  rather than assumed to follow from the sibling fix.
- The same pattern of gap, this time in `Mesh::RevolveProfile()`:
  `profile` had its own validation, but the sibling `revolve_segments`
  parameter didn't - and unlike the `TessellateGrid()` family's `NaN`
  failure mode, a debug run showed this one wasn't even a clean crash.
  `revolve_segments=0` silently produced a near-empty, faceless mesh
  (`VertexCount=2`, `FaceCount=0`) instead of failing loudly, since each
  ring's per-segment vertex loop simply never ran at that count. Fixed by
  throwing `std::invalid_argument` below 3 (the minimum for a
  non-degenerate ring), verified across four values (`0`, `1`, `2`, and a
  negative count) rather than just the one zero case.
- The same pattern again, one function over: `Mesh::Torus()`'s
  `major_segments`/`minor_segments` had the identical unvalidated gap -
  a debug run confirmed a `0` value for either produces a fully empty
  (0 vertices, 0 faces) mesh rather than a crash or thrown error, since
  the corresponding vertex-generation loop simply never runs. Fixed the
  same way as `RevolveProfile()`: throws `std::invalid_argument` below 3,
  checked independently for each parameter.
- The most serious find in this whole validation-gap sweep:
  `Mesh::Cylinder()`/`Mesh::Cone()`'s shared `BuildCircularDiskCap()`
  helper had no `circle_segments` check, and unlike every gap above,
  `circle_segments=0` wasn't a clean empty-mesh or crash failure at all -
  a debug run showed it built an *empty* trim polygon, which this
  kernel's own `Brep::Tessellate()` treats as "no trim at all" (see its
  own `trim_loop.empty()` branch), so the untrimmed ~1.2x-oversized
  square cap surface got tessellated and swept whole into a real,
  plausible-looking, but completely wrong solid (V=578, F=1152, sized
  like the square cap, not the requested circle) - the kind of silent
  wrong-answer bug the "debug-test-first, verify actual behavior before
  asserting" discipline this file has followed all along exists to catch.
  `circle_segments=1`/`2` separately threw a confusing, unrelated error
  from deep inside `ExtrudeCappedSolid()` ("cap has no boundary") instead
  of a clear one at the actual mistake's source. Fixed by validating
  `circle_segments >= 3` directly in the shared helper, so both
  `Cylinder()` and `Cone()` now throw the same clear
  `std::invalid_argument` immediately.
- The same silent "empty trim means untrimmed" footgun, closed at its
  actual source rather than only at the one call site that had tripped
  over it: `Brep::TrimmedPlanarFace()` itself never validated
  `trim_loop_uv`'s point count. A debug run confirmed the identical
  behavior at the public API level - an empty `trim_loop_uv` silently
  tessellated to the *whole untrimmed surface* (V=25, F=32 for a plain
  5x5 grid, not the caller's intended trim), while a 1- or 2-point loop
  silently tessellated to nothing instead. Fixed by requiring at least 3
  points (a real minimum - fewer isn't a closed polygon at all), so every
  current and future `TrimmedPlanarFace()` caller gets this protection
  directly, not just `Cylinder()`/`Cone()`'s own now-fixed call site.
- The most serious finding in this whole validation-gap sweep, and the
  reason it kept going one layer further than the previous fix: checking
  whether `NurbsSurface::TessellateGridClippedExact()` itself (one layer
  below `TrimmedPlanarFace()`, and directly callable on its own) had the
  same gap turned up that an empty `trim_polygon` there doesn't just
  tessellate wrong - a debug run showed it **segfaults**. Root-caused by
  reading the crash site: the concave-clipping path's `ClipPolygon()` has
  a "no boundary crossings at all" fallback that unconditionally
  dereferences `clip[0]` to test which polygon contains the other - a
  real out-of-bounds vector access when `clip` (the `trim_polygon`) is
  empty, not merely a logic bug. `IsSimplePolygon()`'s existing check
  couldn't have caught this even in principle (it passes a too-short
  polygon vacuously, since there's nothing for it to find a crossing
  between). Fixed the same way as every gap above: `trim_polygon.size() <
  3` now throws `std::invalid_argument` before any of that code runs,
  verified with a dedicated test across all three of `trim_polygon`'s
  empty/1-point/2-point cases (confirming a clean throw where the debug
  run had shown a crash), not just the zero-point case that was found
  first.
- The milder sibling of that segfault, closed for completeness:
  `TessellateGrid()`'s whole-cell path goes through `PointInPolygon()`
  instead of the crashing `ClipPolygon()` concave path, and
  `PointInPolygon()` itself turned out to already be safe on a too-short
  polygon (an unsigned `n - 1` underflow that's never dereferenced, since
  the loop bound is also 0) - so this one was "only" a silent
  fully-empty-mesh result for a non-null `trim_polygon` with fewer than 3
  points, confirmed by a debug run, not a crash. Still fixed the same
  way, in the shared `TessellateFromValues()` helper so both
  `TessellateGrid()` and `TessellateGridNonUniform()` get the same
  `std::invalid_argument` check.
- The same gap, one parameter over: a too-short polygon in
  `hole_polygons` was silently ignored entirely rather than rejected -
  `PointInPolygon()` reports every point "outside" it, so a hole with
  fewer than 3 points excludes nothing, and a debug run confirmed the
  outer trim alone determined the result (the full un-holed grid,
  V=49/F=72) instead of an error. Fixed in the same shared
  `TessellateFromValues()` helper, checked for every polygon in
  `hole_polygons` independently.
- `NurbsCurve::SetWeightAt(i, weight)`/`NurbsSurface::SetWeightAt(i, j,
  weight)`: the real construction-side gap `WeightAt()`/`IsRational()`
  (a chunk ago) left - without it, this kernel could only ever *read* a
  rational curve/surface someone else built (e.g. via
  `ON_Circle::GetNurbForm()`), never build one of its own directly.
  Delegates to `ON_NurbsCurve`/`ON_NurbsSurface::SetWeight()`, which
  auto-promotes a non-rational object to a genuinely rational one the
  first time a non-1.0 weight is set. A real, non-obvious behavior found
  by a debug run rather than assumed: this does NOT rescale the control
  point's own stored coordinates to compensate - OpenNURBS' internal
  representation is literally `(x, y, z, w)` evaluated as `(x, y, z) /
  w`, and `SetWeight` only touches `w` - so raising a weight also moves
  that control point's own represented position, not just its blending
  influence. Verified with two independent hand-derived exact
  homogeneous-blend results (a rational-quadratic-Bezier curve midpoint,
  and cross-checked on a bilinear surface midpoint too, not assumed to
  generalize from the curve case alone) that contradict the naive
  "same position, more influence" intuition. Also caught and fixed a
  real bug in this method's own first draft: its no-op-detection check
  called `WeightAt(i)` before validating `i`, which would have hit
  `WeightAt()`'s own documented unchecked out-of-bounds read on a
  rational object for a bad index - confirmed by a debug run that it
  actually segfaulted, fixed by bounds-checking `i`/`j` directly against
  `ControlPointCount()`/`CVCountU()`/`CVCountV()` first.
- `NurbsCurve::ControlPointAt(i)`/`SetControlPointAt(i, point)` and
  `NurbsSurface`'s equivalents: the real gap `SetWeightAt()`'s own
  position-shifting behavior made sharp - without a way to *read* a
  control point's actual current Euclidean position, a caller had no way
  to know where a control point ended up after changing its weight.
  `ControlPointAt()` delegates to `ON_NurbsCurve`/`ON_NurbsSurface::
  GetCV(..., ON_3dPoint&)`, which divides out the weight on a rational
  object - confirmed by a debug run to return exactly
  `original_point / new_weight` after a `SetWeightAt()` call, the
  reader-side mirror of that method's own finding. `SetControlPointAt()`
  has its own real, verified caveat: `ON_NurbsCurve`/`ON_NurbsSurface::
  SetCV(..., const ON_3dPoint&)`'s own documentation says it resets a
  rational object's weight at that index to 1.0 as a side effect -
  confirmed by a debug run, not assumed - so it doesn't compose with
  `SetWeightAt()` on the same control point the way two independent
  setters normally would. Both getters/setters bounds-check `i`/`j`
  directly (throwing `std::out_of_range`) rather than trusting
  `GetCV()`/`CV()`'s own unchecked indexing - the same
  `SetWeightAt()`-derived discipline, verified this time by confirming
  a debug run's out-of-range calls raised the exception cleanly rather
  than crashing, not merely inferred from reading `WeightAt()`'s
  known caveat.
- `NurbsCurve::KnotCount()`/`KnotAt(i)`/`SetKnotAt(i, value)` and
  `NurbsSurface`'s `direction`-parameterized equivalents: the last piece
  of "full read/write access to the underlying NURBS representation"
  this chunk's own `ControlPointAt()`/`WeightAt()` work had been
  building toward - there was previously no way to inspect or adjust a
  curve/surface's own knot vector at all without reaching into `raw()`.
  Verified concrete values, not just the formula: a degree-2,
  3-control-point curve's `KnotCount()` is exactly `cv_count + degree -
  1 = 4`, and its clamped-uniform knot vector is exactly `[0, 0, 1, 1]`
  (each end repeated `degree` times, not `order` times); a non-square
  (5x3 control points, degree 2x1) surface's two directions were
  cross-checked independently at `[0, 0, 1, 2, 3, 3]` (u) and `[0, 1,
  2]` (v). A real, deliberate asymmetry found by reading both
  underlying calls rather than assumed symmetric with `WeightAt()`'s own
  pattern: `Knot(i)` has no bounds check at all (a genuine unchecked
  out-of-bounds read this wrapper guards against by throwing
  `std::out_of_range`), but `SetKnot(i, value)` already bounds-checks
  internally and returns `false` safely - so `SetKnotAt()` returns
  `Result::Failed` instead of throwing, matching that real underlying
  safety profile rather than adding a redundant, inconsistent throw.
- `NurbsCurve::InsertKnotAt(knot_value, multiplicity)`: real Boehm's-
  algorithm knot refinement via `ON_NurbsCurve::InsertKnot` - genuinely
  adds control points (and knots) without changing the curve's own
  shape at all. Validates `knot_value` is strictly interior to the
  domain and `multiplicity` is between 1 and `Degree()`, throwing
  `std::invalid_argument` otherwise (checked directly rather than
  trusting the underlying bool return, so a caller gets a clear reason).
  A real floating-point wrinkle found by testing rather than assumed: an
  initial draft of this method's own test asserted exact bit-for-bit
  equality of `PointAt(t)` before and after insertion, and that
  assertion genuinely **failed** - the refined control net evaluates the
  same true shape through a different arithmetic path, rounding
  differently in the last couple of ULPs, so the test (and the method's
  own doc comment) now correctly describes this as "unchanged to a
  tight numerical tolerance," not exact equality.
- `NurbsSurface::InsertKnotAt(direction, knot_value, multiplicity)`:
  same real Boehm refinement as the curve version, `direction`-
  parameterized. Testing it turned up a second real, easy-to-misread
  API nuance, confirmed rather than assumed: `multiplicity` means
  "ensure the knot ends up with at least this multiplicity," not
  "always insert this many new copies" - inserting an already-existing
  knot value at a multiplicity it already has is a genuine no-op (no
  new control points or knots), yet `ON_NurbsCurve`/`ON_NurbsSurface::
  InsertKnot` still return `true` since their own postcondition is
  already satisfied. Found by literally hitting this case by accident
  (the surface's own default clamped-uniform knot vector already had an
  interior knot at the first value tried) rather than going looking for
  it, then confirmed and documented on both `NurbsCurve::InsertKnotAt()`
  and this method with a dedicated test each.
- `NurbsCurve`/`NurbsSurface::MakeRational()`/`MakeNonRational()`: thin
  wraps of `ON_NurbsCurve`/`ON_NurbsSurface::MakeRational()`/
  `MakeNonRational()`, with `Result::NoOpAlreadySatisfied` when already
  in the requested state. `MakeRational()` is genuinely shape-preserving
  (confirmed by testing, not assumed) - every control point simply gets
  an explicit weight of 1.0, the same implicit weighting a non-rational
  object already had.
  `MakeNonRational()` is the surprising one, and the most significant
  finding of this chunk: it does **not** generally preserve shape,
  despite each individual control point ending up at its own
  geometrically "correct" Euclidean position (`ON_NurbsCurve`/
  `ON_NurbsSurface::MakeNonRational()` divides each control point's raw
  coordinates by its own weight to get there). The reason is that
  forcing every weight to 1.0 afterward blends those now-corrected
  points with ordinary polynomial basis functions instead of the
  original rational ones - a mathematically different curve/surface
  whenever the original weights varied. Verified concretely, not just
  argued: a genuine radius-5 circle's own radius after `MakeNonRational()`
  is no longer constant (exactly 5.0 only at points that already had
  weight 1.0, up to ~5.28 elsewhere), and a genuine radius-3 sphere's
  distance from center likewise varies by more than 0.1 across a sampled
  grid instead of staying exactly 3.0 - it stops being a circle/sphere
  at all, not just a slightly-off approximation of one. Only genuinely
  shape-preserving when every weight was already equal (the mirror-image
  condition of `MakeRational()`'s own guarantee).
- `NurbsSurface::RemoveKnotAt(direction, knot_index, tolerance,
  &max_deviation)` (new `src/surface_edit.cpp`): the exact inverse of
  `InsertKnotAt()`, a gap OpenNURBS itself leaves open (verified against
  the v8 source: `ON_NurbsCurve`/`ON_NurbsSurface` have `InsertKnot`,
  `IncreaseDegree`, `Extend`, `Trim`, `Split`, but no knot removal at
  all - `RemoveSpan` deletes a whole span's geometry, which is a
  different operation). Implemented from scratch as Tiller's algorithm
  (Piegl & Tiller A5.8, single removal), run in homogeneous 4D on every
  row/column of the control net at once so it handles rational surfaces
  too, with the knot vector converted between ON's compressed storage
  (which drops the two redundant end knots) and the textbook form the
  algorithm indexes. The important property: knot removal is only
  shape-preserving when the surface genuinely has the extra continuity
  at that knot, and this kernel's rule is never to ship a silently-wrong
  approximation, so the method computes a *rigorous* upper bound on the
  max 3D deviation the removal would cause - the algorithm's own control-
  net discrepancy, which bounds the surface error because it is the
  coefficient of a single non-negative, partition-of-unity basis
  function (on a rational surface the discrepancy lives in homogeneous
  space and is converted to a Euclidean bound via P&T eq. 5.30's
  `TOL = d * w_min / (1 + |P|_max)`, looser but still rigorous) - and
  only commits when that bound is within `tolerance`, otherwise leaving
  the surface bit-identical and still reporting the bound. Verified with
  computed geometry, not just counts: inserting a knot into a wiggly
  bicubic and removing it again recovers every original control point
  to 1e-9 and the sampled surface to 1e-9; the same on a rational
  radius-3 sphere (every sampled point still exactly radius 3);
  removing a genuinely non-removable knot is refused at 1e-6 with the
  net untouched, then committed at a permissive tolerance where the
  sampled deviation on a 129x33 grid is confirmed to be both nonzero and
  <= the reported bound (and > 25% of it, so the bound isn't vacuous);
  and removing one multiplicity of a sphere's quarter-point double knot
  has its sampled deviation <= the rational bound. A mutation check
  (skipping the algorithm's control-point recomputation) makes four of
  those checks fail, so the test genuinely exercises the math. Knot
  vectors that aren't clamped in that direction (periodic surfaces)
  are refused honestly rather than half-handled: their wrapped control
  points would need matching edits this doesn't do. Companion
  `MaxSampledDeviationFrom(other, nu, nv)` is the sampled (lower-bound)
  deviation measurement the tests use, exposed because refit/rebuild-
  style operations need the same report.
- `NurbsSurface::SetDomain(direction, t0, t1)`: reparameterizes one
  direction onto exactly `[t0, t1]` - an affine rescale of that
  direction's knot vector and nothing else, delegating to
  `ON_NurbsSurface::SetDomain` after reading its source to confirm it's
  the real linear knot map, not a stub. Verified by computed geometry:
  every U knot lands at the affine image of its old value to 1e-12, the
  same *normalized* (u, v) evaluates to the same 3D point to 1e-12 on a
  13x13 grid, and every control point stays bit-identical. Reported as
  a no-op on the current domain, refused for an empty/reversed interval.
- `NurbsSurface::Rebuild(u_count, v_count, u_degree, v_degree, out,
  &max_deviation, u_samples, v_samples)`: Rhino's Rebuild / a
  Parasolid-style refit, as the *global tensor-product least-squares*
  solution rather than the "sample the surface and use the samples as
  control points" shortcut (which shrinks any curved surface toward its
  interior, since a B-spline never passes through its interior control
  points). Piegl & Tiller A9.7: sample the source on a parameter grid,
  least-squares-fit every sample row in U with the two end control
  points pinned (eq. 9.63-9.67), then fit every column of those
  intermediate points in V the same way - with gridded parameters and
  one shared clamped-uniform knot vector per direction the row-then-
  column solve is the exact tensor-product least-squares solution, so
  the corners are interpolated exactly and the result reproduces the
  source's own parameterization and domain. The deviation report is a
  genuine measurement (both surfaces evaluated on a grid twice as fine
  as the fit samples, offset half a step so it never re-uses a sample
  the fit already saw, plus the four boundary curves), documented as a
  sampled lower bound rather than claimed exact. Verified with computed
  geometry: refitting a wiggly 6x4 bicubic onto its own net recovers
  every control point to 1e-9 with deviation < 1e-9; onto a 9x7 net
  whose knots are a superset it is still exact (< 1e-9, confirmed by an
  independent 65x65 sampling); onto a 5-CV net that can't hold the
  source's knots the reported deviation is > 1e-3, agrees with an
  independent 129x65 sampling within 5%, the corners stay exact to
  1e-12, and the least-squares fit deviates less than a third as much
  as the sample-as-control-point construction on the same net; a
  rational radius-2 sphere refit to a 16x10 cubic net reports a
  deviation between 1e-6 and 0.02 that bounds the measured radius
  error within 5%. A mutation check (writing samples straight into the
  control points instead of solving the normal equations) fails five of
  those checks.
- `NurbsSurface::MatchEdge(fixed_direction, at_min, target,
  target_fixed_direction, target_at_min, continuity, &report,
  cross_scale)` (Position/Tangent/Curvature = G0/G1/G2): Rhino's
  MatchSrf, done as exact NURBS algebra on the control net rather than
  by moving control points onto sampled target points (the app's own
  pre-existing `MatchSrfCommand` did the latter - see below for where
  this replaces it). G0 makes the two boundary curves the *same* curve:
  the target edge is oriented to match (reversal auto-detected from
  corner distances, reported), reparameterized onto this edge's domain
  (`SetDomain`, shape-preserving), both sides degree-elevated to the
  higher edge degree and knot-refined to their union knot vector
  (`InsertKnot`, both shape-preserving so the target is never altered),
  then this surface's edge control row is replaced by the target's
  (homogeneous coordinates, so a rational target's weights carry over -
  making this surface rational too if it wasn't). G1 additionally sets
  the next row so this surface's cross-boundary first derivative equals
  `-scale` times the target's inward one, via the standard clamped-
  B-spline end-derivative formula `p / (V_{p+1} - V_1) * (R_1 - R_0)`;
  G2 sets the third row the same way for the second derivative
  (`scale^2`). With a constant `scale` this is exactly C1/C2 in the
  reparameterization `v' = scale * v`, hence genuinely G1/G2 - not
  merely "close" - and `scale` defaults to this surface's own mean
  cross-derivative speed over the target's (so the match keeps this
  surface's parameterization rather than adopting the target's), or can
  be forced. The method runs its own self-check by evaluation after
  editing (never trusts the algebra blindly): `report`, if non-null,
  receives the measured max position/tangent/curvature residual along
  the edge, and the whole edit is rolled back to
  `Result::Failed` if any residual exceeds a tight tolerance scaled by
  the surfaces' own size - so a caller can never silently receive a
  wrong match. The far edge is left untouched (verified: the cross
  direction is only widened enough, via degree elevation / one knot
  insertion, to hold the rewritten rows, both shape-preserving).
  Verified with real differential-geometry checks, not just "didn't
  crash": G0 gives 1e-12 boundary-curve coincidence but leaves a real
  crease (`|n_S x n_T|` stays > 1e-3); G1 additionally drives that
  cross-product to < 1e-12 (unit normals genuinely parallel along the
  whole edge) while still leaving second-derivative mismatch, and
  forcing `cross_scale = 1` reproduces exact textbook C1/C2 against the
  target's own raw derivatives; G2 drives the cross-product of the
  Gaussian curvatures to < 1e-9 along the edge and confirmed on this
  surface's other three edge/direction combinations too (u=max, a
  reversed target orientation); matching onto a genuine rational sphere
  cap makes the candidate's edge an exact circle on the sphere (every
  sampled point exactly radius 2, Gaussian curvature exactly `1/r^2`
  along the whole edge to 1e-9, every resulting weight still positive).
  Refused (surface untouched) for a periodic/unclamped edge or cross
  direction, or Curvature continuity against a degree-1 target (no
  second derivative to match) - both checked directly. A mutation
  (breaking the tangent-row derivative-scaling coefficient) makes the
  self-check itself catch the corruption and fail closed - 9 of the
  new checks fail under it, confirming the tests (and the guard) are
  real.
- `include/dino8/kernel/tolerance.h` - the kernel's tolerance policy,
  closing the one "Known gaps" point below that had stayed accurate
  since chunk 1 ("no tolerance-management policy defined yet"). Three
  primitives in the same classes Parasolid/ACIS distinguish - an
  absolute distance (`tolerance::kDistance`, 1e-6 model units), a
  relative fraction (`kRelative`, 1e-6 of a local size) and an angle
  expressed as the unit-vector dot-product deficit every existing call
  site already computes (`kAlignment`, 1e-6, ~0.08 degrees) - plus two
  degeneracy floors (`kZeroVector` 1e-9, `kZero` 1e-12) and the DERIVED
  constants specific operations read: `kWeld` (= `kDistance`; `Mesh::
  MergeAndWeld()`'s default and brep.cpp's `kBrepWeldTolerance`),
  `kPlanarityRelative` (= `kRelative`; `IsRingPlanar()`'s end-cap test in
  `LoftClosedRings()`), `kOnGridLineFraction`/`kGridNudgeFraction` (=
  `kRelative`; `TessellateGridClippedExact()`'s concave-trim grid-line
  nudge) and `kEdgeJoin` (1e-4; `RemoveNakedMicroEdge()`'s/
  `ReplaceEdgeCurve()`'s defaults and the floor `MergeCoplanarFaces()`
  re-welds exposed naked edges with), with `DistanceForSize(size)` =
  `max(kDistance, size * kRelative)` for the "absolute floor, relative
  above it" pattern brep.cpp used by hand. Deliberately a NAME-AND-ROUTE
  pass, not a tuning pass: every constant carries exactly the literal it
  replaced, and that nothing measurable changed is proven two ways - the
  BooleanCombineGeneral sweep (`tests/general_boolean_sweep.cpp`) output
  is byte-for-byte identical (`cmp`) before and after, and the full
  smoke suite stays green. `TestTolerancePolicyValuesAreTheOnesInForce`
  pins the routing by BEHAVIOUR at both sides of each threshold, not by
  re-reading the header: `MergeAndWeld()` with no tolerance argument
  welds vertices 0.4*`kWeld` apart and leaves 3*`kWeld` distinct, and
  `LoftClosedRings()` accepts an end ring 0.5*`kPlanarityRelative`*extent
  out of plane and throws at 3x. Proven to actually watch the routed
  site: temporarily editing `kPlanarityRelative` to 1e-7 in the header
  moved `LoftClosedRings()`'s own accept/reject threshold with it (the
  behavioural checks still passed against the moved value; only the
  "reads 1e-6" pin failed) - a literal left behind in mesh.cpp would
  have failed the 3x rejection instead. Honest limits, unchanged from
  before and now stated in one place: `kDistance` is not scaled by model
  size, so a 1e-6 gap on a 1e6-unit model is below double precision's
  own resolution there.

  A second, later routing pass (same commit series, same name-and-route
  discipline, checked the same two ways - byte-identical sweep, full
  green ctest - one family at a time) added `tolerance::kTinyDistance`
  (1e-9) and `RelativeDistance(size)` = `max(kTinyDistance, size *
  kRelative)` - `DistanceForSize()`'s purely-relative sibling, for the
  `std::max(1e-9, x * 1e-6)` radius/edge-length fit tolerances brep.cpp
  used by hand (a cylinder/cone axis-distance check, the plain-quad seam
  and general-boolean linear-fit tolerances) - and routed the several
  `1.0 - 1e-6` unit-vector-alignment checks (cylinder/cone/Steinmetz
  parallelism, `MergeCoplanarFaces()`'s own plane-normal match) through
  the existing `kAlignment`, plus `mesh.cpp`'s remaining `1e-9`/`1e-12`
  degeneracy floors (the volume/centroid/vertex-normal zero checks,
  `IsPlanarRingSimple()`/`IsRingPlanar()`'s own degenerate-triple
  fallback) through `kZeroVector`/`kZero`. Verified by BOTH value and
  behaviour: `TestTolerancePolicyValuesAreTheOnesInForce` pins
  `kTinyDistance`/`kAlignment` and `RelativeDistance()`'s own floor/scale
  by value, and the unchanged pass/fail of the existing cylinder, cone,
  Steinmetz and planar-ring tests (whose fits and alignment checks route
  through these exact sites) is what proves nothing measurable moved -
  the same claim the byte-identical sweep makes for the boolean path.
  What's left unrouted after both passes: `TrimmedPlanarFace()`'s
  clipping-boundary sample-count heuristics and a handful of one-off
  epsilons with no natural family yet (`FromMixedFaces`' 5%
  surface-padding margin, `boolean.cpp`'s own `kConvexTol`/
  `kMinCylinderPairPieceAngle`/`kTiny`) - narrower, more special-purpose
  constants than the two families routed so far, left for a pass with
  its own dedicated verification rather than folded in here.

- `Brep::Extrude()` / `Revolve()` / `Loft()` / `Sweep1()` / `Pipe()`
  (`src/sweep.cpp`): the first B-rep-level sweep-class operations here
  (Parasolid's sweep/spin/loft/pipe class). Until now every "solid from
  a profile" in this kernel was mesh-level (`Mesh::ExtrudeCappedSolid()`,
  `RevolveProfile()`, `LoftClosedRings()`), and the app's own Extrude/
  Revolve leaned on OpenNURBS' `ON_BrepExtrudeFace`/`ON_BrepRevSurface`
  while its Loft fit a surface *through the sections as control points*
  (so the surface did not actually pass through the sections). These
  five build real NURBS surfaces and real `ON_Brep` topology:
  - **Exact math where the shape allows it, verified not asserted.** An
    extrusion is the degree-(p, 1) tensor product of the profile and the
    direction (a 2x3x5 rectangle's welded mesh volume is exactly 30 at
    every division pair tried, to 1e-9); a revolution is Piegl & Tiller
    A8.1's rational quadratic (one 90-degree arc per quadrant, the full
    circle from an exact quadrant table so the v=2*pi control points are
    bit-identical to v=0 and `IsClosed(1)` holds) - a revolved
    semicircle passes `NurbsSurface::IsSphere()`, a revolved slanted
    segment `IsCone()`, both at 1e-9; a degree-1 loft between coaxial
    circles passes `IsCone()`; `Pipe()` along a straight rail passes
    `IsCylinder()` (two stations, degree 1 - the exact extrusion, not a
    sampled tube). A loft/sweep INTERPOLATES its sections: global
    B-spline interpolation (chord-length station parameters averaged
    over the control-point columns, averaged knots, a dense LU with
    partial pivoting shared across every column) after making the
    sections compatible shape-preservingly (clamp if periodic, common
    rationality, degree elevation to the max, `[0, 1]` domain, merged
    knot refinement - knots within 1e-12 treated as one). Checked
    directly: `S(u, v_k)` reproduces three circles / four squares to
    1e-9, and three circles with linearly varying radius come out as the
    exact frustum again. Between sweep stations the surface is an
    interpolant, not the true swept shape (a quarter-arc pipe with 16
    stations is within 0.7% of Pappus' volume; documented as such).
  - **Closed lofts/sweeps** solve the periodic interpolation problem
    over a cyclic system (knots at the station parameters for odd
    degree, at midpoints for even - Schoenberg-Whitney for the cyclic
    case), build the ON-periodic surface, then `ClampEnd()` it (and, for
    even degree, `ChangeSurfaceSeam()` back to section 0) so callers get
    an ordinary clamped surface with `IsClosed(1)`; a degree-1 closed
    loft is built on the clamped path through the wrapped list, since
    `ON_NurbsSurface::IsPeriodic()` is only defined for degree >= 2.
    Eight unit squares around a radius-5 ring give exactly the 8-gon
    prismatic ring at degree 1 and within 0.3% of Pappus' 10*pi at
    degrees 2 and 3; a 32-station closed pipe is within 0.8% of the
    torus 2*pi^2*R*r^2. Sweep frames are rotation-minimizing (double
    reflection); on a closed rail the accumulated twist is spread evenly
    so the last frame meets the first.
  - **Real topology.** Faces go in through `ON_Brep::NewFace(surface,
    vid, eid, bRev3d)`, OpenNURBS' own way of stitching untrimmed faces:
    singular sides become singular trims, a closed section's seam a seam
    trim, and each cap passes the wall's own boundary edge index so the
    shared edge is literally shared. `raw().IsValid()` and
    `raw().IsSolid()` hold for every closed result (the older `Box()`/
    `Sphere()` factories still fail `IsValid()`, as brep.h has always
    said), and every closed fixture in the tests also asserts that NO
    face needed an `m_bRev` flip - the orientation rules (a closed
    profile counterclockwise about the extrusion direction; a revolve
    region clockwise in the (rho, z) half-plane, because
    `S_u x S_v = C' x e_phi` is `-e_rho` for a counterclockwise one - a
    sign the first draft got backwards and the test caught; a loft's
    first section counterclockwise about the direction to the second)
    orient the body outward by construction, with a coarse-tessellation
    volume sign as a cross-check that flips the whole body if a rule is
    ever wrong for an input.
  - **Caps are planar fans to a kernel point**, not `TrimmedPlanarFace()`
    polygons. D(u, v) = (1 - v) X + v C(u) is a genuine planar NURBS
    face whose north boundary IS the wall's boundary isocurve, so the
    cap and the wall sample that edge identically at ANY
    `(u_divisions, v_divisions)` - `TessellateToClosedMesh()` is a
    closed manifold at (8, 8), (12, 5) and (5, 12) for every capped
    fixture, which a fixed polygon trim can never give (it matches one
    wall sampling only, and its exact-clip boundary adds T-junctions at
    grid crossings even then). X is found by exact half-plane
    intersection over a dense sampling of the boundary (the region's
    kernel), then re-verified against the true curve (the angle of
    C(u) - X must be strictly monotone). The honest limit: a region with
    an empty kernel (a C-shape, a spiral) cannot be capped this way and
    throws when a cap is asked for - an L-shape is fine (its kernel is
    the inner corner's quadrant). A revolve's end caps for an open
    profile put X ON the axis segment between the profile's endpoints,
    so the two caps share that segment as two literal `ON_BrepEdge`s and
    the quarter-cylinder is a real solid (4 edges, 3 vertices). A full
    revolve's off-axis end circle gets a disc cap built TRANSPOSED (the
    boundary along the cap's v, matching the wall's v-direction edge),
    the fix for the one asymmetric-divisions case the untransposed fan
    got wrong (found by the (12, 5) check, confirmed closed after).
  - **Two pre-existing gaps this exposed and fixed.** (1)
    `Mesh::MergeAndWeld()` kept faces that welding had collapsed - a
    pole row's (a, a, b) zero-area triangles - so `Brep::Sphere()`'s own
    `TessellateToClosedMesh()` had NEVER been a `Mesh::IsClosedManifold()`
    (confirmed on the pre-fix build: 512 faces, 32 degenerate, not
    closed); it now drops a collapsed triangle and reduces a quad with
    one repeated corner to a triangle, leaving `Volume()`/`Area()`
    unchanged (a collapsed face contributes zero to both) - the sphere
    is now closed with exactly 480 faces, and
    `TestMergeAndWeldDropsCollapsedPoleTriangles` fails on the pre-fix
    build. (2) `CollectPlainQuadFaces()` (the asymmetric-divisions seam
    pass in `Brep::Tessellate()`) took any planar untrimmed face for a
    bilinear quad; a planar fan cap is planar and untrimmed but its four
    domain corners are (X, X, C(b), C(a)), and building THAT as a
    bilinear patch would tessellate the cap as a zero-width sliver. It
    now requires four distinct corners. This one is defensive: the pass
    only rebuilds a quad that shares a straight edge with ANOTHER plain
    quad, and no fixture today puts a fan cap in that position (a cap's
    only neighbour is its curved wall), so the guard closes a latent
    path rather than a failing test.
  - **Limitations stated, not hidden.** A closed profile touching the
    axis (a rectangle with one side on it) is refused - the touching
    side would sweep to a degenerate zero-area band inside one face -
    with the open L-shaped profile named as the exact alternative; a
    partial revolve of an open profile with an off-axis endpoint is not
    cappable here; sections are not auto-aligned or re-seamed; no
    2-rail sweep with scaling, no variable-radius pipe yet (draft-angle
    extrusion is closed below).
    A capped body reverses its section internally when needed for
    outward orientation, so the wall's u may run opposite to the input
    curve (the closed-loft test matches section corners as a set for
    that reason).
- `Brep::ExtrudeTapered(profile, direction, draft_angle, cap)`
  (`src/sweep.cpp`): `Extrude()` with a draft angle - the wall leans
  instead of running straight, Parasolid/ACIS's TAPER option on a swept
  protrusion and AutoCAD `EXTRUDE`'s `Taper angle`. The app already had
  an approximate version (`ExtrudeTaperedCommand`,
  `dino8-app/src/commands/cmd_surface.cpp:1191`: the top section is the
  profile SCALED ABOUT ITS CENTROID by `tan(draft) * height` - exact only
  for a profile centered on its own centroid with uniform radius, i.e. a
  circle; wrong for anything else, since a real draft wall is supposed to
  move every boundary point by the same PERPENDICULAR distance, not scale
  the whole shape toward one interior point). This is the first
  kernel-native, genuinely-correct one, built on `Loft()`'s own exact
  degree-1 ruled wall between the profile and an in-plane-offset copy of
  it translated to the far end, with three honestly different fidelity
  levels depending on what the profile actually is:
  - **Circle/arc profiles are exact.** `NurbsCurve::OffsetInPlane()`'s own
    Arc/Circle case offsets to an exact concentric arc/circle, so a
    drafted circular boss or hole is a genuine NURBS cone frustum wall
    (`NurbsSurface::IsCone()` holds), volume verified against the closed
    form `(pi*L/3)(r0^2 + r0*r1 + r1^2)` to 0.3% (tessellation chord
    error, same bound the existing frustum tests use) - and the top
    section's own radius is checked directly against `r0 -
    L*tan(draft_angle)` to 1e-9, not just the aggregate volume.
  - **Convex polygon profiles are ALSO exact - a new closed-form
    algorithm, not a reuse of `OffsetInPlane()`'s general branch.** A
    multi-segment polyline (a rectangle, a hexagon, any convex profile
    built as straight `Polyline()` segments) needs `OffsetInPlane()`'s
    OWN least-squares refit branch, which cannot get a sharp corner right
    (it blurs it) and, for a CLOSED polygon whose seam sits exactly at a
    corner, does not even reproduce a closed curve at all (the tangent -
    and so the offset direction - genuinely differs on the two sides of
    that corner, so the refit's own forced-equal endpoints split into two
    different points). `OffsetConvexPolyline()` (`src/sweep.cpp`) instead
    computes the EXACT planar miter-join point at every vertex in closed
    form - `V' = V + distance*(n0 + n1) / (1 + n0.n1)`, derived directly
    from the two edges' half-angle bisector, not fit or iterated - and is
    proven safe for a convex input by a cheap, exact check applied to
    every result: each offset edge must stay a POSITIVE multiple of its
    own original direction (a convex polygon offset uniformly can only
    self-intersect by inverting an edge first, so checking for that
    directly is a complete, not heuristic, validity proof). Convexity
    itself is checked up front (all turns the same sign) and a concave
    profile is refused rather than risked - the general polygon-offset
    self-intersection problem this kernel already discloses as open
    elsewhere (`PARITY_MAP.md`, "Offset self-intersection / invalid-loop
    removal") is not attempted here. Verified two ways: an isotropically-
    tapered square's SIDE FACES are themselves planar (a real geometric
    fact for uniform-scale taper, checked by hand: the four corners of
    each side quad are coplanar), so unlike the circular case the
    tessellated volume is EXACT (not merely within tolerance) at any
    division, `1e-9` against the closed-form frustum-of-pyramid volume
    `A0*h*(k0^2 + k0*k1 + k1^2)/3`; and the actual top corner positions
    are checked directly against the hand-computed offset points, not
    just the volume.
  - **A general (non-arc, non-polyline) planar profile** falls through to
    `OffsetInPlane()`'s own general least-squares branch, inheriting its
    already-documented exactness/approximation split and its own
    curvature-based self-intersection guard - no new limitation invented
    for this function.
  - **Sign is anchored to `direction`, not to whichever way
    `IsPlanar()`'s fit happened to come out.** `NurbsCurve::
    OffsetInPlane()`'s own Arc/Circle case self-corrects its sign against
    the shape's independently-known true radial direction (its own doc
    comment), but its Line/general case and this function's own convex-
    polygon path do not - both use "distance > 0 grows along this
    curve's own fitted plane normal," and that normal's SIGN is an
    otherwise-arbitrary artifact of `IsPlanar()`'s fit (confirmed by
    reading `ON_Curve::IsPlanar()`/`ON_NurbsCurve::IsPlanar()`: the plane
    itself is built from the curve's own control points, independent of
    the tolerance argument, so the SAME curve always gets the SAME fitted
    normal, but a mirrored or differently-wound copy of the same shape
    can fit to the opposite one). `ExtrudeTapered()` canonicalizes once
    (flips the reference normal, and the delegated `OffsetInPlane()`
    distance sign with it, whenever the fit came out opposite
    `direction`) so "positive `draft_angle` shrinks moving along
    +`direction`" holds for every profile, not just the ones whose fit
    happened to agree - checked directly: the same circle built with its
    defining plane's normal flipped gives the exact same frustum for the
    same `draft_angle`.
  - **Degenerate cases refused, not guessed.** A non-finite or
    out-of-`(-pi/2, pi/2)` `draft_angle`; a non-planar profile; a
    `direction` not parallel to the profile's own plane normal (an
    oblique draft would need the in-plane offset and the axial
    translation decomposed separately, not attempted); a non-convex
    multi-segment profile; a draft/height combination that would fold
    the offset curve through itself (an arc shrinking past its own
    radius, a polygon edge inverting past its own inradius, or a general
    curve's own curvature-based guard) - every one throws
    `std::invalid_argument` naming which check failed, and `draft_angle
    == 0` delegates to `Extrude()` itself exactly rather than taking a
    numerically-noisier path through the offset machinery for no reason.
- `SubD::SetEdgeSharpness(p0, p1, sharpness, point_tolerance)`: real
  Pixar/OpenSubdiv-style semi-sharp (variable-weight) creasing, closing a
  gap `FromControlMesh()`'s own `crease_at_double_edges` parameter left
  open since it landed - that flag only ever gives a binary sharp/smooth
  split (a permanent `ON_SubDEdgeTag::Crease`), with no way to dial in
  anything between "fully smooth" and "fully creased," and no way to
  crease an edge at all without the mesh-double-edge topology trick.
  OpenNURBS' own model for this turned out to already exist and be real,
  not a stub - found by reading, not assumed: `ON_SubDEdge::m_sharpness`
  (`ON_SubDEdgeSharpness`, range `[0, MaximumValue=4]`) is genuinely
  consumed by `ON_SubDimple::GlobalSubdivide()` (it reads
  `e0->IsSharp()`/`e0->Sharpness(false)` and calls `.Subdivided(0/1)` on
  each child edge, both read directly in `opennurbs_subd.cpp`) and by the
  regular-patch evaluator in `opennurbs_subd_limit.cpp` (branches on
  `ON_SubDEdge::IsSharp()` / `ON_SubDVertex::VertexSharpness()` to blend
  face/edge/vertex points toward crease behavior) - the same "declared in
  the public header, actually implemented" pattern this file's SubD
  entries already document for `LimitPoints()`, as opposed to the
  `BrepForm()`/`CreaseEdgeCount()` stubs. The convenience wrapper the
  class comment for those methods once pointed to (`ON_SubD::
  SetEdgeSharpness()`) turned out to be the *unimplemented* one this
  time - grepped across the whole v8.34 source tree, it doesn't exist
  anywhere outside a doc comment - so this method instead calls the same
  low-level primitive OpenNURBS' own `AddEdge(..., ON_SubDEdgeSharpness)`
  overloads call on a freshly-built edge
  (`ON_SubDEdge::SetSharpnessForExperts`), applied here to an edge found
  via the ordinary const `FindVertex`/`FindEdge` accessors (a `const_cast`
  is required to call it, since those accessors are const - safe because
  the call writes exactly one field with no other cached state to
  invalidate, verified by reading `SetSharpnessForExperts`'s own three-line
  body, and because `ON_SubD`'s copy constructor deep-copies its
  `ON_SubDimple` rather than sharing it, so a fresh `SubD::FromControlMesh()`
  result is never aliased with another live `ON_SubD`). Refuses (returns
  `false`, no change made) rather than silently no-op'ing for an
  out-of-range weight, an edge that doesn't exist between the given
  points, or an edge that's already a hard crease (sharpness is
  meaningless there in OpenNURBS' own model). Verified three ways, not
  just read: (1) exact bookkeeping - a 2.5 weight reads back as exactly
  2.5 via `EndSharpness()`, `Subdivided()` subtracts exactly 1.0, and a
  real `Subdivide(1)` call leaves exactly the fold's 2 child edges
  (and no others) reporting `IsSharp()` at exactly the decayed 1.5; (2) a
  `MaximumValue`-weight edge produces the identical exact straight-fold
  subdivision point `TestSubDCreaseAtDoubleEdgeKeepsFoldStraight()`
  already proved for a real hard crease, on the same hinge fixture, while
  an untouched control SubD still rounds the same fold off; (3) the
  refusal cases leave the SubD provably unchanged (a hard crease's
  `CreaseEdgeCount()` unaffected by the refused call). One honestly-
  scoped limitation: this exposes one constant weight per edge; OpenNURBS
  also supports a per-end-variable sharpness (linearly interpolated along
  the edge, decaying differently at each end), which this wrapper doesn't
  expose - a caller needing that must use `raw()` directly.
- `SubD::SetCrease(p0, p1, crease, point_tolerance)`: retags an existing
  edge Crease or back to Smooth after construction - closing PARITY_MAP.md's
  subd_mesh-category "Crease tagging / un-tagging as a kernel operation"
  [missing] item directly (kernel::SubD had no `SetCrease`/`ClearCrease`
  at all; the app's `SubDCrease` command edited `ON_SubDEdge` tags
  directly, bypassing this wrapper entirely). Unlike `SetEdgeSharpness()`
  just above - which needs a `const_cast` onto a low-level "for experts"
  primitive because OpenNURBS' own convenience wrapper for THAT is
  unimplemented - this delegates to a genuinely public, fully-implemented
  `ON_SubD::SetEdgeTags()`, verified by reading its body in
  `opennurbs_subd.cpp` rather than trusting the name: it does real work
  beyond the one edge's own tag, reclassifying both endpoint vertices
  (Smooth/Dart/Crease/Corner, recomputed from their new incident-crease
  count), clearing any leftover `SetEdgeSharpness()` weight on either
  transition, and invalidating cached evaluation state - bookkeeping a
  caller hand-editing `raw()` would otherwise have to reproduce itself.
  Verified geometrically, not just by reading: `SetCrease(true)` on the
  hinge fixture's smooth fold edge produces the bit-identical straight-
  line subdivision point at (0.5, 0, 0) that both a construction-time
  `crease_at_double_edges=true` crease and a `SetEdgeSharpness`-at-
  `MaximumValue` semi-sharp edge already independently proved above -
  three different mechanisms, same underlying OpenNURBS crease math, same
  measured result. Returns `false` (a real no-op, not an error) for a
  point pair with no matching edge or an edge that already carries the
  requested tag, matching `ON_SubD::SetEdgeTags`'s own 0-changed
  convention.
- `SubD::IsValid()`: the SubD-level counterpart to `Mesh::
  IsClosedManifold()`, closing PARITY_MAP.md's subd_mesh "SubD non-
  manifold / multi-body validity checks" [missing] item (this class had
  no `Check()`/`IsValid()` at all - a caller could only discover a broken
  SubD the hard way, whatever `ON_SubD` happened to do internally).
  Delegates to the real, non-stub `ON_SubD::IsValid()`, verified by
  reading its implementation: it walks every level's vertices, edges and
  faces checking cross-reference and tag consistency, a genuine
  structural check. The one subtlety worth documenting: it's called with
  OpenNURBS' own sentinel (`(ON_TextLog*)1`, low bit set, never
  dereferenced - `ON_SubD::IsValid` masks that bit off again before
  touching it, read directly in `opennurbs_subd.cpp`) rather than
  `nullptr`, because a bare `nullptr` does NOT suppress `ON_SubD::
  IsValid()`'s own `ON_Error()` call on failure - only the sentinel does.
  Skipping that would have meant every legitimate "no" (e.g. checking a
  SubD mid-edit) spammed OpenNURBS' global error log as a side effect of
  asking a yes/no question. Verified both ways: a default-constructed
  (never built) `SubD` - the simplest genuinely-invalid case, no hand-
  corruption of `raw()` needed - reports `false`, and a real
  `FromControlMesh()` result reports `true` and stays `true` through
  actual `Subdivide()` calls.
- Verified (not a code change): PARITY_MAP.md's subd_mesh category listed
  "Mesh <-> SubD round trip fidelity (density-preserving)" as only
  [partial] - `FromControlMesh()` and `ToApproximateMesh()` both existed,
  but nothing had ever checked the round trip was actually density-
  preserving. It is, at level 0 (no `Subdivide()` call - `ToApproximateMesh()`
  just re-extracts the still-unrefined control net): confirmed directly,
  with a throwaway `tests/scratch_test.cpp` program before writing the
  permanent regression test, that a triangulated closed box round-trips
  through `SubD::FromControlMesh()` -> `ToApproximateMesh()` with its
  exact 8-vertex/12-face count, every vertex position exactly preserved,
  identical volume (winding preserved, not just positions), and stays a
  closed manifold - and that a genuinely QUAD mesh (SubD's natural face
  type, not something this kernel's other tessellators produce) round-
  trips with its faces still genuine quads, not silently re-triangulated.
  Both are now permanent regression tests
  (`TestSubDMeshRoundTripIsExactAtLevelZero`), so this is a corrected,
  verified claim rather than an assumed one.
- `Mesh::CheckReport::duplicate_faces` + `Mesh::RemoveDuplicateFaces()`:
  the fifth `Check()`/repair pair, closing a real gap `degenerate_faces`/
  `RemoveDegenerateFaces()` didn't cover - two perfectly valid, non-
  degenerate faces sitting exactly on top of each other (the same
  vertex indices, in the same cyclic order or its exact reverse - a
  common "import appended the same geometry twice" defect), which a
  per-face degeneracy test alone can never catch, since each one, taken
  alone, is a fine triangle. Identity is computed as the lexicographically
  smallest of a face's 2n rotations (n forward + n reversed, n = 3 or 4)
  - winding-direction-agnostic and rotation-agnostic, so a triangle, its
  same-winding rotated repeat, AND its opposite-winding repeat are all
  correctly recognized as the same polygon, not just an exact index-array
  match. `RemoveDuplicateFaces()` keeps the first occurrence (in face-array
  order) and drops every later duplicate, reusing the same
  `CompactUnusedVertices()` helper the degenerate-face repair already
  uses. Distinct from `duplicate_vertices`: two faces built from
  different vertex INDICES that happen to sit at the same 3D position is
  a `CloseNakedEdges()` problem, not this one - `duplicate_faces` is
  about index-identical polygons, not merely coincident ones. Verified
  with a fixture carrying one triangle repeated 3 ways (an index-order
  repeat, a rotated repeat, an opposite-winding repeat) alongside one
  genuinely distinct triangle: `Check()` counts exactly 2 duplicates (not
  3 - the first occurrence is the baseline, not a duplicate of itself),
  `RemoveDuplicateFaces()` removes exactly those 2, and the survivor is
  provably the first occurrence, not an arbitrary one.
- `SubD::FromNurbsSurface(surface, u_divisions, v_divisions)`: closes
  PARITY_MAP.md's subd_mesh "SubD from NURBS/B-rep conversion (reverse
  of ToNurbsPatches)" [missing] item for a single untrimmed surface (a
  full Brep -> SubD conversion - matching faces and creases across a
  whole solid or polysurface - is a materially bigger problem, not
  attempted here). Evaluates a `u_divisions x v_divisions` grid of
  points across the surface's own parameter domain and takes each cell
  as one genuine QUAD SubD face, then hands that straight to the
  already-existing `FromControlMesh()`. Deliberately NOT built on
  `NurbsSurface::TessellateGrid()` despite the obvious temptation to
  reuse it: that method always TRIANGULATES each cell (it exists for
  mesh-boolean work), which would start every SubD face irregular before
  `Subdivide()` even ran once - `ToNurbsPatches()` only gives an exact
  limit patch on regular, all-quad faces, so triangulating here would
  quietly defeat the entire point of building a SubD cage in the first
  place. Honestly scoped as an APPROXIMATION of the input surface, not a
  lossless conversion: a Catmull-Clark limit surface over a regular quad
  reproduces a uniform bicubic B-spline (see `ToNurbsPatches()`'s own
  doc comment), not an arbitrary NURBS surface's true shape between grid
  points (non-uniform knots, non-cubic degree, rational weights - none
  of that survives flat-grid sampling); the one case this IS exact for
  is a flat/bilinear input, verified directly: a hand-derivable
  `P(u,v) = (u, v, 0)` fixture (the same one `TestSurfaceNormalAt()`
  already relies on) converts to a 5x5-vertex, 16-quad-face SubD whose
  level-0 control net reproduces all 25 grid points to within 1e-6 of
  their exact closed-form positions - not merely "close," measured.
- `SubD::CapBoundaryLoop(start, point_tolerance)`: closes PARITY_MAP.md's
  subd_mesh "SubD hole/opening capping at kernel level" [missing] item -
  the SubD-level counterpart to `Mesh::FillSmallHoles()`, but genuinely
  SubD-native rather than a ported mesh trick: `ON_MeshFace` tops out at
  4 indices, so `FillSmallHoles()` needs a new centroid vertex and a
  triangle fan, but `ON_SubDFace` supports any edge count directly, so an
  n-sided hole becomes exactly ONE new n-gon face - no extra vertex, and
  (being a real SubD face like any other) immediately a genuine,
  further-subdividable part of the control net. Identifies the loop by
  walking from one of its own boundary vertices (every boundary vertex
  has exactly 2 naked edges, so the walk - follow a naked edge, take the
  far vertex's OTHER naked edge, repeat - is unambiguous except at a
  "bowtie" vertex where two loops touch, the same acknowledged ambiguity
  `Mesh::NakedEdgeLoops()` already documents for its identical case), then
  hands the collected edges to the real, working
  `ON_SubD::AddFace(const ON_SimpleArray<ON_SubDEdge*>&)` (verified by
  reading its implementation: it validates the loop genuinely closes and
  computes each edge's orientation from shared vertices automatically,
  not a stub). The one subtlety worth documenting: the loop's own edges
  are tagged Crease purely because they were a boundary (OpenNURBS' "an
  open SubD's boundary edges are themselves always creases" convention,
  not because anyone asked for a sharp seam), so after capping they're
  retagged Smooth via the same `ON_SubD::SetEdgeTags()` primitive
  `SetCrease()` already wraps - otherwise the cap would leave a
  permanent, unintended crease ring exactly where the hole used to be. A
  caller who DOES want that sharp ring can call `SetCrease()` again
  afterward. Verified with a flat 2x2 quad grid (9 vertices, 4 faces, one
  8-edge boundary loop, one fully interior vertex): capping adds exactly
  1 new 8-sided N-gon face and 0 new vertices/edges, all 8 former-
  boundary edges read back as Smooth (not Crease) afterward, capping at
  the interior vertex is refused (no naked edge to start from), and
  capping again once the SubD is fully closed is refused too.
- `SubD::Transform(xform)`: the same missing piece `Mesh::Transform()`
  already closed for `Mesh`, but this class never had at all - no way to
  move, rotate, scale, or mirror a SubD once built (baking the transform
  into the control mesh only works BEFORE `FromControlMesh()`, and is
  impossible after `Subdivide()` has already discarded the original
  mesh). Delegates to the real `ON_SubD::Transform` (verified by reading
  `ON_SubDimple::Transform`'s own implementation: it transforms every
  level's vertices, detects a similarity transform to preserve cached
  subdivision/limit points instead of discarding them, and updates
  texture/color mapping and symmetry state). One caveat documented
  explicitly because it's genuinely easy to miss: a MIRROR (negative-
  determinant `xform`) only moves positions - it doesn't touch any
  face's vertex winding, the same convention `Mesh::Transform()` already
  follows (`ON_Mesh::Transform` flips stored normal VECTORS on a
  negative determinant but never reorders `ON_MeshFace::vi[]`). The
  result stays perfectly `IsValid()` (a uniform coordinate transform
  can't break the topology's internal edge/face winding agreement - the
  same reason a wholly `Mesh::FlipNormals()`-ed mesh stays a valid
  closed manifold), just "inside-out" relative to the mirrored geometry;
  there's no `SubD`-level `FlipNormals()`/`UnifyNormals()` counterpart
  here (a real, disclosed gap), so a caller mirroring a SubD should
  handle that at the `ToApproximateMesh()`/`ToNurbsPatches()` stage
  instead. Verified on a closed quad-box SubD: every control-net vertex
  shifts by an exact translation and scales by an exact uniform factor
  (hand-derived, not approximate), a mirror leaves `IsValid()` true
  (measured, not just claimed), and a genuinely invalid (NaN-carrying)
  `xform` throws rather than silently handing back a garbage copy.
- `Mesh::Offset(distance)` / `Mesh::Thicken(distance)`: the mesh-level
  offset/thicken this class never had at all (distinct from the Brep-
  level offset/shell another session owns). `Offset()` moves every
  vertex along its own `ComputeVertexNormals()` direction - built
  directly on that already-existing, already-documented primitive
  rather than recomputing normals its own way, so it inherits that
  method's own area-weighted, per-triangle-contribution correctness (and
  its own honest "zero vector for an unreferenced vertex" edge case).
  Honestly NOT topologically robust - a plain per-vertex push with no
  self-intersection detection or repair, the same disclosed tradeoff
  every simple normal-offset mesher has. `Thicken()` builds a genuine
  solid shell from an OPEN mesh: an `Offset()` copy stitched to the
  original along every naked edge with a new quad wall face, the
  original layer flipped to face the material correctly. The walls need
  no separate orientation logic at all - each is built directly from
  `Check()`'s own `naked_edge_list`, already recorded in the correct
  outward-walking direction by that field's own long-standing
  documentation, so getting `Thicken()` right was really just trusting
  data that already existed. Refuses (`std::invalid_argument`) a zero
  distance and an already-closed input (closed-mesh hollowing is a
  materially different, unattempted problem). Verified by hand, not
  just plausibly: a single flat unit-square face thickened by exactly 1
  produces an EXACT unit cube - 8 vertices, 6 faces, closed manifold,
  volume exactly 1.0 - derived corner-by-corner and edge-by-edge before
  writing the test (which triangle's flip direction and which wall
  vertex order produce an outward-facing cube), not verified after the
  fact by adjusting signs until a check passed.
- `Mesh::CheckReport::non_manifold_edge_list`: localizes what
  `non_manifold_edges` had only ever COUNTED - a real gap `naked_edges`
  never had, since `naked_edge_list` already existed for it. Undirected
  (a 3+-face edge has no single walking direction the way a naked or
  orientation-conflicted edge does), one entry per non-manifold edge as
  its two vertex indices with the smaller first, in the order first
  encountered walking the mesh's own face list - the same convention
  `naked_edge_list` already established. Deliberately NOT wired up as a
  repair input the way `naked_edge_list` feeds `FillSmallHoles()`: which
  faces should stay grouped together at a 3+-face edge is a judgment
  call this class still doesn't make (documented already, unchanged).
  Verified with the simplest possible non-manifold fixture - a "book" of
  3 triangles sharing one spine edge, every other edge naked - Check()
  reports exactly 1 non-manifold edge and the list contains exactly that
  edge's own two vertices.

## Blending build log (Parasolid "blend/chamfer" class, chronological)

Exact edge blends live in `include/dino8/kernel/fillet.h` /
`src/fillet.cpp` (`FilletConvexEdge`, the tapered `FilletConvexEdgeTapered`
overloads, and the chamfers below). Each entry here records what was
closed, the closed form it was checked against, and what is still
honestly out of scope.

- **`ChamferConvexEdge(solid, p0, p1, distance_i, distance_j)` and
  `ChamferConvexEdgeAngle(solid, p0, p1, distance_i, angle_from_i)`** -
  the planar sibling of `FilletConvexEdge`: a two-distance (Parasolid
  "chamfer by two ranges") or distance+angle chamfer of one straight,
  convex edge between two planar faces. Because the blend face is a
  plane, the whole result is a `Brep::FromMixedFaces` of planar faces
  only: no dense polygonal notch, no sagitta tolerance, every shared
  boundary a single exact `ON_LineCurve` edge. The one genuinely new
  piece of geometry versus the fillet is the END CONDITION, and it is
  MORE general than the fillet's: at each endpoint the third face's
  sharp corner is replaced by the two points where the chamfer's rails
  pierce that face's plane (`Q_i = rail_i /\ plane_k`, which provably
  lies on face k's own existing edge with face i, since `rail_i` lies in
  plane i), so an end face OBLIQUE to the edge - the case
  `FilletConvexEdge` still leaves untouched, because a cylinder's
  oblique section is an ellipse - is closed exactly here, because a
  plane's oblique section is just another line. Four or more faces at an
  endpoint, or a curved neighbour, throw `std::invalid_argument` rather
  than returning a chamfer whose end floats unattached; a free edge end
  (no third face at all) is honestly left open, as the fillet already
  does. The angle form is a thin dispatch: `distance_j = distance_i *
  sin(angle) / sin(theta + angle)` from the law of sines in the
  chamfer's own triangular cross-section (theta = interior dihedral),
  rejecting angles outside `(0, pi - theta)`. Verified in
  `tests/test_basic.cpp` (`TestChamferConvexEdge*`), not merely argued:
  a unit box chamfered (0.3, 0.2) is `IsValid()`/`IsManifold()`-closed/
  `IsSolid()` with 7 faces, 15 edges, 10 vertices and tessellated volume
  `1 - 0.3*0.2/2 = 0.97` to within 1e-6 (the residual is `ON_Mesh`'s own
  float vertex storage, ~1e-9 here); a hand-built hexahedron whose +x end
  face is the oblique plane `x = 1 + 0.3y` chamfers to a closed solid of
  volume `1.15 - (di*dj/2)*(1 + 0.1*di)` (the removed prism's triangle
  centroid at `y = di/3` swept to the oblique plane - exact, checked to
  1e-6) with the rail/oblique-face piercing vertex landing exactly at
  `(1 + 0.3*di, di, 1)`; the 45-degree angle form reproduces the
  symmetric `(di, di)` chamfer and the 30-degree form's own plane makes
  exactly 30 degrees with face i; and non-positive/oversized distances, a
  non-edge diagonal, out-of-range angles and an input already carrying a
  fillet's curved face are all rejected. Still out of scope, disclosed:
  a chamfer on an edge with a curved adjacent face, a concave edge, and
  chaining several chamfers on one solid (the second call is rejected by
  `PlanarFaces()` only if the first left a curved face; two chamfers are
  both planar and do chain).
- `Brep::Check()` and the healing operations built on it - the Parasolid
  "check/heal" class (`PK_BODY_check`, `PK_BODY_repair`) this kernel had
  no counterpart to: `ON_Brep::IsValid()` is one bool, reports every
  `Box()`/`Sphere()`-built Brep as invalid for lacking topology (see
  brep.h's class comment), and says nothing about WHERE or HOW MUCH.
  `Check(tolerance, sliver_width)` returns a structured `CheckReport`: a
  list of `CheckIssue`s, each with a kind, an index into the raw
  `m_E`/`m_F`/`m_T`/`m_L` arrays, a second index where one applies (the
  other face, the vertex, the edge), a 3D `location` and a `measure`,
  plus `topology_valid`/`is_closed`/`is_oriented` summary flags and a
  `Count(kind)` accessor. Eleven kinds: `NakedEdge` (trim count < 2),
  `NonManifoldEdge` (>= 3), `InconsistentFaceOrientation` (two faces
  walking a shared edge the same way - `ON_Brep::IsManifold()`'s own
  `m_bRev3d XOR m_bRev` rule, plus the `LoopDirection()` term the app
  layer's `OrientBrepFaces` already carries for a clockwise-stored
  loop), `DegenerateEdge` (16-segment sampled length within tolerance),
  `DegenerateFace` (outer loop's 3D samples collinear within tolerance:
  width about the longest chord, an exact zero-area test for a polygon
  that a Newell-area test gets wrong on a full-cylinder loop whose two
  circles cancel), `SliverFace` (width within `sliver_width`),
  `EdgeVertexGap`, `TrimEdgeGap` (the trim's 3D image at start/middle/
  end vs the edge curve at the matching, `m_bRev3d`-aware parameter),
  `LoopGap` (consecutive trims not meeting, measured in 3D through the
  surface), `InvalidTrim` (no edge/curve/surface, or 2D endpoints
  outside the surface domain) and `SelfIntersectingLoop`
  (`detail::IsSimplePolygon` on the same `SampleLoop()` samples
  `Tessellate()` would use). Both gap kinds honour a TOLERANT edge: a
  gap is reported only above `max(tolerance, edge.m_tolerance)` (or the
  vertex's own), so a deliberately tolerant join is not re-reported as
  a defect. Repairs, all on this class's own `ON_Brep` with the side
  tables cleared the way `MergeCoplanarFaces()` already does:
  - `JoinNakedEdges(tol)` joins coincident naked-edge pairs (the
    kernel's own `WeldCoincidentNakedEdges`, previously private to
    `MergeCoplanarFaces()`), then RECORDS the measured trim-vs-edge gap
    on each surviving edge's `m_tolerance` (and each vertex's) - after
    `SetTolerancesBoxesAndFlags()`, which resets those (see
    `FixUnsetEdgeTolerances`' own comment) - and orients the faces.
    Checked directly: a top face built 1e-4 above its sides leaves 8
    naked edges; `JoinNakedEdges(2e-4)` joins 4 pairs and exactly those
    4 edges carry `m_tolerance == 1e-4` (to 1e-12), after which the
    default `Check()` is clean and `IsValid()`/`IsSolid()` hold.
  - `UnifyNormals()` - the app's `OrientBrepFaces` brought into the
    kernel, plus the step it lacked: for a closed shell, the sign of
    `TessellateToClosedMesh(4, 4).Volume()` decides outward, so an
    inside-out shell (every face flipped: perfectly consistent, volume
    -1, nothing for a consistency check to find) is fixed too.
  - `RemoveDegenerateFaces(tol)`/`RemoveSliverFaces(width)` delete the
    faces `Check()` reports and re-join the exposed neighbour edges with
    `JoinNakedEdges` - a 1e-5-wide strip on a box top becomes three
    tolerant edges of tolerance 1e-5, an 8e-7 strip closes exactly.
  - `RemoveDegenerateEdges(tol)` collapses via `ON_Brep::CollapseEdge()`
    - which, found by testing rather than assumed, leaves the loop's
    OTHER junction open by the collapsed edge's own parameter-space
    length (a 7.3e-7 (u, v) residual `ON_Brep::IsValidLoop()`'s
    1e-10-relative match test rejects); `CloseLoopGapsWithinTolerance`
    moves the next trim's 2D start exactly onto the previous end
    wherever the 3D gap is within tolerance, and the result is
    `IsValid()` again.
  - `CapPlanarHoles(tol)` chains naked edges through their vertices
    (straight edges only - a curved one is refused rather than left as
    an unjoined polygonal cap), fits a plane (Newell normal, every
    vertex within `DistanceForSize(extent)`), and builds the cap
    THROUGH `FromPlanarFaces()` rather than `ON_BrepTrimmedPlane()`:
    the first attempt used the latter and produced a valid, solid
    `ON_Brep` whose welded mesh nonetheless had 32 naked T-junction
    edges, because `FromMixedFaces()`'s planar surfaces are 5%-padded
    and exact-clipped (grid rows at 0.225) while the unpadded cap's
    grid rows sat at 0.25 - a real, measured mismatch, not a theory.
  - `TessellateToClosedMeshTolerant()` - the plain call plus a
    true-distance `Mesh::CloseNakedEdges()` over the mesh's naked seams
    at `max(kWeld, 2 * largest edge tolerance)`. Deliberately a
    separate entry point, not a change to `TessellateToClosedMesh()`:
    notched conical fillet caps already carry genuine edge tolerances
    and their plain-path output is pinned by existing tests, so
    widening that weld silently was not an option in this pass. It also
    closes the sub-`kWeld` seam grid snapping misses (two points 8e-7
    apart straddling a 1e-6 snap-cell boundary stay unwelded - the
    8e-7-strip fixture's plain mesh is open for exactly that reason).
  `Mesh` gets the same layer: `Check()` (naked/non-manifold/conflicting
  edge counts, degenerate faces by repeated index or height within
  tolerance, duplicate vertices by true distance through a 27-cell grid
  lookup, and the naked-edge list in face order), `NakedEdgeLoops()`,
  `CloseNakedEdges(tol)` (true-distance weld restricted to boundary
  vertices - the lowest index survives at its own position, collapsed
  faces are dropped, quads that lose a corner become triangles),
  `FillSmallHoles(max_extent)` (one triangle for a 3-loop, a centroid
  fan otherwise, each fill triangle walking its boundary edge in
  reverse of the existing face; loops larger than the bound are left
  open on purpose) and `UnifyNormals()` (BFS flip plus the same
  outward-by-volume step). Every repair is tested on a DELIBERATELY
  BROKEN fixture with hand-derived expectations, and every diagnostic
  is checked for firing with the right index/location/measure before
  the repair and going silent after it: a flipped face (4
  `InconsistentFaceOrientation` issues naming face 1 and each of its 4
  neighbours; 1 or 11 flips; volume +1, and -1 -> +1 for the inside-out
  shell), a dropped face (4 naked edges at z=1; one cap; 6 faces / 12
  edges; the PLAIN mesh closed with volume exactly 1), the 1e-4-lifted
  top (above), the hairline strips (above; volume within the strip's
  width of 1 via the tolerant tessellation), a shared 8e-7 micro edge
  (one `DegenerateEdge` of measured length 8e-7 on a 13-edge solid;
  collapsed to 12 edges / 8 vertices, `IsValid()`, volume 1), and on
  the mesh side a dropped triangle (3-loop, 8 vertices / 12 faces after
  the fill) and a dropped quad (4-loop, 9 / 14), a backwards triangle
  (3 conflicts) and an inverted box (0 conflicts, volume -1, 12 flips),
  a duplicated corner (4 naked edges, 2 duplicate vertices, 1 weld) and
  a corner copied 1e-4 away (refused at 1e-6, welded at 2e-4). Honest
  limits: face width and edge length are SAMPLED (the loop's own trim
  samples, 16 segments per edge), not exact minimum-width computations;
  a degenerate face split as a T-junction leaves naked edges after
  removal (no endpoint pair coincides), reported, never hidden; and
  `CapPlanarHoles()` handles straight-edged planar holes only.
- `IntersectCurves(a, b, opt)` (CCX, `surface_intersect.h`): the
  curve/curve counterpart to the existing `IntersectSurfaces()` (SSX)
  and `IntersectCurveSurface()` (CSX) - the public OpenNURBS SDK ships
  none of the three. Recovered from a previous session's uncommitted,
  mid-flight worktree edits (found coherent and nearly finished on
  inspection - the header, implementation and regression test all
  present and mutually consistent - rather than re-derived from scratch;
  credited here honestly). Two general space curves only meet at
  isolated points, so unlike SSX it returns points (`CurveCurveHit`: both
  parameters, the refined point, the residual), not curves. Seeded like
  CSX: both curves are sampled into polylines at a resolution driven by
  `opt.mesh_tolerance`, every segment pair whose padded boxes overlap is
  checked with Ericson's exact closed-form closest-points-between-two-
  segments computation (the same textbook `Mesh::ClosestPoint()` already
  cites), and every close-approach pair seeds a damped Gauss-Newton on
  `(ta, tb)` minimizing `|A(ta) - B(tb)|` via the shared `NewtonSolve()`;
  hits within `4 * opt.tolerance` of an accepted one are dropped as the
  same crossing. Verified with three hand-derivable exact cases, not
  plausible-looking ones: two lines forming an X cross at exactly
  `(5, 5, 0)` with `ta = tb = 0.5` (a line's parametrization is linear in
  position, so the geometric midpoint IS the domain midpoint); the same X
  with one line lifted to `z = 1` (skew, never meeting) reports zero hits
  rather than the in-plane crossing its XY projection suggests; and a
  genuine rational-NURBS circle (`ON_Circle::GetNurbForm`) against a line
  through its center hits at exactly `(+/-radius, 0, 0)`, in `ta` order.
  Honest limitation, stated on the declaration: not intended for curves
  coincident over a real span (the residual is ~0 along the whole
  overlap, so the finite seeding/dedup reports a handful of isolated
  points, not the shared span). Verified against the full 76-case
  `general_boolean_sweep` too (byte-identical to the baseline - it's a
  new function nothing else calls yet, but it lives in
  `surface_intersect.cpp`, so that check is the rule, not optional).
- `Mesh::VolumeMassProperties()` closes the second-moment half of mass
  properties that `Volume()`/`GetCentroid()` never covered: the complete
  inertia tensor about both the world origin and the centroid (products
  of inertia in the `ixy = integral of x*y dV` convention Rhino and
  every engineering table use, with the tensor's off-diagonals being
  their negatives - stated on the `MassProperties` struct so nobody has
  to guess the sign), principal moments (ascending) with a right-handed
  orthonormal principal frame, and radii of gyration. The public
  OpenNURBS SDK has no mesh mass-property implementation at all
  (grepped: no `ON_Mesh::VolumeMassProperties` anywhere in the source),
  so this is from scratch. EXACT, not sampled - Eberly's "Polyhedral
  Mass Properties (Revisited)": each of the ten volume integrals of
  `{1, x, y, z, x^2, y^2, z^2, xy, yz, zx}` is reduced by the divergence
  theorem to a closed-form polynomial in every triangle's three vertices
  (the same principle `Volume()` already uses for the zeroth moment), so
  a polyhedron's moments are its true moments, and a tessellated curved
  solid's converge to the smooth shape's exactly as `Volume()`'s does.
  The principal decomposition delegates to OpenNURBS'
  `ON_Sym3x3EigenSolver`, read and verified as a real implementation
  (Jacobi rotation to tridiagonal form plus a closed-form tridiagonal
  solve) rather than one of its declared-but-unimplemented stubs.
  Verified against hand-derived closed forms, all confirmed by a debug
  run before being asserted: a 2x3x4 box (quad faces) gives exactly
  `V(b^2+c^2)/12 = 50, 40, 26` about its centroid, exactly `200, 160,
  104` and products `36, 72, 48` about the origin (the parallel-axis
  theorem by hand), principal moments exactly `(26, 40, 50)` on its own
  z/y/x axes; the identical box as 12 triangles matches to 1e-9 (the
  quad path's second triangle is counted); the box rotated 0.7 rad about
  a skew axis and translated keeps principal moments `(26, 40, 50)` to
  6e-7 (a rigid-motion invariant) while its world-frame products of
  inertia become clearly nonzero (the tensor genuinely rotated, it
  wasn't re-diagonalized); and a real curved body with a closed-form
  tensor, a torus (`R=3, r=1`, 96x48 segments), lands within 0.1% of
  `M(R^2 + 3r^2/4)` about its axis and `M(R^2/2 + 5r^2/8)` about a
  diameter, with its two in-plane moments exactly equal (96-fold
  symmetry makes the in-plane tensor isotropic) and its centroidal
  tensor unchanged to 2e-8 relative when the whole torus is built at
  `(10, -5, 2)` instead of the origin. Throws `std::invalid_argument` on
  an inside-out (negative-volume) mesh rather than returning negated
  moments, and on an empty/zero-volume one - both checked.
- Three mesh spatial queries that didn't exist in any form, each exact:
  - `Mesh::FireRay(origin, direction)` returns every crossing of a ray
    with the mesh, sorted by parameter (`RayHit`: t, point, face index,
    entering/leaving) - `ContainsPoint()` had always fired a ray
    internally but only ever counted crossings, so nothing could say
    WHERE a ray lands or on which face (a pick, a visibility test). The
    existing private Moller-Trumbore helper was refactored to return its
    parameter and barycentrics (`ContainsPoint()` now wraps it with the
    identical `t > 1e-12` rule, so its results are unchanged - the
    full smoke suite and the 76-case boolean sweep both confirm that).
    A hit exactly on a quad face's shared diagonal is reported once,
    via a hair (1e-9) of barycentric slack so round-off can't make both
    triangles reject it; the honest degenerate case (a ray exactly
    grazing an edge or vertex shared by two faces) is stated, not
    hidden. Verified: a +x ray through the [0,2]^3 box hits at exactly
    t=1 and t=3 (faces 4 then 5, entering then leaving); doubling the
    direction halves both t without moving the points (t is in units of
    `direction`, documented); a ray from inside hits once, leaving at
    exactly t=0.7; misses and away-pointing rays return empty; a ray
    through both x faces' diagonals reports 2 hits, not 4; an oblique
    ray's two hits land at the hand-derived plane crossings (0, 0.7,
    0.55) and (2, 1.7, 1.05).
  - `Mesh::DistanceTo(other)` is the exact minimum surface/surface
    distance with the closest pair of points and faces - the clearance
    query `ClosestPoint()` (point-to-mesh only) couldn't answer. Exact
    per triangle pair through all three feature families the minimum
    can live in: vertex/triangle (Ericson's region test, 6 pairs),
    edge/edge (closed-form segment/segment closest points, 9 pairs,
    now shared with the CCX seeding via `detail/segment3d.h`), and
    edge-pierces-triangle (which the first two families can't see -
    nothing on either boundary is at distance 0, yet they cross - so
    without it a crossing pair would report the nearest vertex's
    positive distance, silently wrong). Per-pair bounding-box reject
    against the running best; no BVH. Verified: parallel-face boxes at
    exactly 1 (and a debug run showed the tie resolving on a corner
    that belongs to the bottom face, so the test asserts only the tie-
    invariant properties, not a face index it can't claim); corner-to-
    corner boxes at exactly `sqrt(3)` between (2,2,2) and (3,3,3), and
    symmetric under swapping the operands; a hand-built skew-
    perpendicular edge pair whose unique minimum (checked by hand
    against every vertex/triangle candidate: sqrt(13), 5, sqrt(8), 5,
    plane distances 3.29 and 2.63) is exactly 2 between the two edges'
    midpoints; a piercing pair reporting 0 at exactly (1,1,0) where the
    nearest vertex is 1 away; boxes sharing a face at 0.
  - `Mesh::ClashWith(other)` classifies two closed solids as `Clear`,
    `Touching`, `Intersecting`, `ThisInsideOther` or `OtherInsideThis`.
    The first draft used edge-pierces-face predicates and was
    abandoned before it ever ran, on paper: the most ordinary CAD clash
    - two equal-height boxes overlapping in plan - has EVERY edge/face
    crossing landing exactly on a face's edge or lying in a face's own
    plane, degenerate for any such predicate, so it would have
    misreported the commonest case as not intersecting. Instead it's
    decided from the exact overlap VOLUME, `vol(this ∩ other)` from the
    existing Manifold-backed `BooleanCombine()` (exact predicates with
    symbolic perturbation, built for coincident geometry), with
    `DistanceTo()` deciding Touching vs. Clear when there's no shared
    volume. The volume tolerance is relative (1e-6) because `ON_Mesh`
    stores single-precision vertices, so a touching pair with non-
    representable coordinates can carry a round-off sliver. A debug run
    confirmed the boolean itself gives exactly 2.0 for the equal-height
    case and exactly 0 (an empty mesh) for the shared-face case before
    the classifications were asserted. Verified: boxes 1 apart Clear;
    sharing a face, an edge, or only a corner all Touching (they meet,
    share no volume); equal-height plan overlap AND generic overlap both
    Intersecting; containment both ways, including a part touching its
    container's wall from inside and an identical pair (ThisInsideOther,
    documented). A real gap the debug run caught in this function's own
    precondition check: a lone open triangle has a nonzero SIGNED
    `Volume()` (its origin tetrahedron doesn't cancel), so "volume > 0"
    let an open mesh through to Manifold's own less specific error -
    fixed by checking `IsClosedManifold()` directly first, as the
    documented precondition says.
- `SubD::LimitPoints()`: the EXACT Catmull-Clark limit-surface point and
  normal of every control-net vertex - closing the "not exact limit-
  surface evaluation" item this file's own "What's still not done" list
  carried since chunk 2, at least for the vertices. A corrected
  assumption, found by reading rather than trusting: `subd.h`'s class
  comment (and that list) said OpenNURBS' public API ships no exact
  limit evaluator, but `ON_SubDVertex::SurfacePoint()`/`SurfaceNormal()`
  are real, non-stub implementations in `opennurbs_subd_eval.cpp` (a
  sector-based computation walking the vertex's incident faces via
  `ON_SubDSectorIterator` - unlike the `BrepForm()`/`CreaseEdgeCount()`
  stubs the same comment correctly documents). Being real in the source
  isn't being correct, so it was verified numerically against the
  standard closed-form limit masks before being trusted, all confirmed
  by a debug run first: on the [-1,1]^3 cube cage every valence-3
  corner's limit point is exactly half its control position, which is
  what Halstead/Kass/DeRose's `(n^2 v + 4 sum(edge nbrs) + sum(diagonal
  nbrs)) / (n(n+5))` gives by hand ((9+4-1)/24 = 1/2), with the outward
  body diagonal as its unit normal; on a flat 3x3 grid the regular
  interior vertex and the boundary-crease edge midpoints stay exactly
  put while the corners land at exactly `(1/6, 1/6, 0)`, the crease
  mask `(e1 + 4v + e2)/6` by hand, with every normal exactly `(0,0,1)`.
  Two further independent cross-checks against the subdivision itself:
  repeated `Subdivide()` brings the control net strictly closer to the
  reported limit points at every level (measured 0.096, 0.016, 4.4e-4,
  1.2e-5 at levels 1, 2, 4, 6 - genuine geometric convergence, the
  defining property of a limit point), and the level-1 net's own limit
  points reproduce the level-0 ones to 1.7e-16 (the limit surface is
  invariant under subdivision). Honest scope on the declaration: this is
  per-vertex only, not evaluation at an arbitrary (u, v) inside an
  irregular face (`ToNurbsPatches()` already covers regular faces
  exactly); a crease/corner vertex's normal is reported for the sector
  of its first face only, since the limit surface genuinely has one
  normal per sector there. `subd.h`'s class comment and the "What's
  still not done" bullet are both corrected rather than left stale.
- `NurbsCurve::Join(other, tolerance)`: joins `other` onto this curve's
  end, in place, into ONE continuous NURBS - the first curve-combining
  operation here (`Trim()`/`Split()`/`Extend()` all cut or stretch a
  single curve; nothing could chain two into the "Join" every modeler
  has). Exact, not a re-fit: `ON_NurbsCurve::Append`, verified by
  reading its source to be a real implementation that degree-elevates
  the lower-degree operand, makes both rational if either is, clamps,
  and splices the knot vectors with `other`'s knots shifted to continue
  from this curve's end. A real `Append` behavior found by that reading,
  which the wrapper exists to guard: it never checks that the curves
  meet - it silently DISCARDS `other`'s first control point in favour of
  this curve's last (its copy loop starts at index 1), so a non-meeting
  pair would get its junction snapped shut and `other`'s first span
  distorted rather than an error. `Join()` therefore requires `other`'s
  start (or, auto-reversing a copy as Rhino's Join does, its end) within
  `tolerance` of this curve's end, throwing `std::invalid_argument` with
  both measured gaps otherwise, and refuses a closed `this`. Verified
  with hand-derivable exact values, confirmed by a debug run first: two
  unit-domain lines meeting at (1,0,0) join to degree 1 with 3 control
  points (the junction merged), domain exactly `[0, 2]`, the junction at
  exactly `t = 1`, `(1,1,0)` at `t = 1.5`, length exactly 3; joining a
  genuine rational degree-2 quarter arc (`ON_Arc::GetNurbForm`) onto
  that polyline elevates it to degree 2 and makes it rational while the
  polyline part is unchanged at its own parameters (degree elevation is
  shape-preserving) and the arc part is reproduced at its own parameters
  shifted by exactly 2 (midpoint `(1 + 1/sqrt2, 3 - 1/sqrt2, 0)`, end
  `(2, 3, 0)`), total length `3 + pi/2`; the reversed-operand case gives
  the identical curve; and both error paths throw. One honest nuance
  the debug run surfaced about an EXISTING method, not this one:
  `Length()`'s default 1000-sample polyline lands a sample exactly on
  the two-line join's kink (so that length is exactly 3) but not on the
  three-piece curve's kinks once its domain is 3.57 long, cutting each
  corner by ~1e-3 - the same polyline approximation `Length()` has
  always documented, so the test measures that case at 200000 samples.
- `PointCloud::KNearest(query, k)` and `PointCloud::PointsWithinRadius(query,
  radius)`: the point cloud's first spatial queries - before this, a
  `PointCloud` could only be built, indexed by raw position (`PointAt(i)`),
  colored, normaled and transformed, with no way to ask "which points are
  near this one", the operation every point-cloud tool (nearest-sample
  lookup, local normal estimation, a "select points near here" pick,
  density/outlier checks) is built on. Exact Euclidean distance to every
  point, brute force - honestly no spatial acceleration structure (no
  kd-tree, no layering on OpenNURBS' own real `ON_RTree`, despite it being
  available), the same "exact over every candidate, no BVH" tradeoff
  `Mesh::DistanceTo()` already documents for its own point-to-triangle
  work; O(`PointCount()`) per query, not claimed to be anything faster.
  Both return `PointCloudNeighbor{index, distance}`, sorted by ascending
  distance with ties (exactly equal distance) broken by ascending index,
  so results are fully deterministic regardless of insertion order -
  verified with a cloud built to have an exact, hand-derivable answer: a
  duplicated point (two coincident points at distance 1, at indices 1 and
  5) confirms both the ascending-distance order AND the index tie-break in
  one case, and the third-place, fourth-place distances are checked
  exactly (`2`, `2*sqrt(2)`, `3`). `KNearest` clamps `k >= PointCount()` to
  "return everything, sorted" rather than erroring (a reasonable request,
  just an easy one), and throws `std::invalid_argument` for `k <= 0` or an
  empty cloud (there is no such thing as "the nearest points" into nothing
  - unlike `PointsWithinRadius`, where zero matches is a perfectly valid
  answer, so an empty cloud or a too-small radius return an empty result,
  never throwing on cloud state - only on a genuinely malformed request, a
  negative radius).
- `Brep::SplitDisjointPieces()`: splits a Brep into its actually-disjoint
  bodies from real topology - the gap `LumpFaceRanges()` cannot close for
  any Brep not itself built by `Compound()`, since that method only
  replays `Compound()`'s own bookkeeping and never inspects the Brep's
  real vertex/edge/trim structure at all (proven in the test itself: a
  genuinely two-body Brep assembled via `FromPlanarFaces()` - two boxes'
  own `PlanarFace` lists in one call, never `Compound()` - still reports
  `LumpFaceRanges() == {{0, 12}}`, one lump, for all 12 faces). Delegates
  the actual graph search to `ON_Brep::LabelConnectedComponents()`
  (verified by reading its source to be a real, non-stub implementation:
  from each unlabeled face it walks every trim on every loop out to that
  trim's own edge and every OTHER face sharing that edge, so two faces
  strung together through any chain of shared edges land in one
  component) and the actual per-piece rebuild to `ON_Brep::
  DuplicateFaces()` (also verified real: a genuine deep copy of exactly
  the referenced surfaces/curves/vertices/edges/trims/loops for that
  piece's own faces). Connectivity is a shared EDGE RECORD, not geometric
  coincidence - `LabelConnectedComponents()` itself documents that it
  does not check vertex-only connections - so two Compound() lumps that
  only touch along a curve (deliberately unwelded - see Compound()'s own
  doc comment) correctly come back as separate pieces here too.
  `DuplicateFaces()` records each duplicate's ORIGINAL face index in its
  own `m_face_user.i` (an OpenNURBS guarantee, not a re-derivation), which
  is exactly the index this uses to carry this class's own six per-face
  side tables (the `PlanarFace`/`CylindricalFace` verbatim records,
  cylinder cap-notch rows, trim/hole polygons, arc runs) over to the
  correct new face; a side table not in lockstep with the original
  `FaceCount()` (a `raw()`-assigned Brep) is treated as absent for every
  piece, the same safe "lose the fast path, never a wrong shape" fallback
  `MixedFaces()` itself already relies on. Verified with a two-box case
  built to have a hand-checkable exact answer (a 1x1x1 cube and a
  1x2x3 box, far enough apart that the vertex welder inside
  `FromPlanarFaces()` cannot possibly join them): `SplitDisjointPieces()`
  returns exactly 2 pieces of 6 faces each, in original-face-index order
  (the lower-indexed body first), each with the source box's own exact
  tight bounding box and each independently `IsValid()`/`IsManifold()`
  (oriented, no free boundary)/`IsSolid()` - a real Brep, not just a face
  list. A single-component Brep (the overwhelmingly common case) returns
  a single-element vector holding an exact untouched copy of itself, and
  a Brep with no faces returns an empty vector - both checked directly.
- `Mesh::GetOrientedBoundingBox()`: a box oriented to the solid's own
  shape rather than the world's - `GetBoundingBox()`'s axis-aligned box
  can waste arbitrary volume on a rotated shape (a long thin box at 45
  degrees gets an AABB nearly twice as wide as it is), which nothing here
  could tighten before. Deliberately NOT a separate PCA over vertex
  positions (the common, simpler technique): that's biased by
  tessellation density (a more finely-meshed region pulls the axes
  toward it even though the true shape hasn't changed), so this reuses
  `VolumeMassProperties()`'s own `principal_axes` instead - computed, like
  `Volume()`/`GetCentroid()`, by the divergence-theorem integral over the
  solid's actual enclosed volume, so the axes depend only on the real
  shape. The two are the same frame by construction, not by coincidence:
  for the standard second-moment convention, inertia tensor `I =
  trace(covariance) * Identity - covariance`, so `I` and the
  volume-weighted covariance matrix share eigenvectors - reusing
  `principal_axes` here IS the volume-weighted PCA frame, mathematically,
  not an approximation standing in for it. `half_extents[k]` is then the
  tightest slab along `axes[k]` containing every one of the mesh's own
  vertices, found by direct search (not estimated), so the box provably
  contains the whole mesh. Inherits `VolumeMassProperties()`'s own
  precondition (closed, consistently oriented, positive volume) and its
  own exceptions, unwrapped. Honest scope: this is the standard
  principal-axis box, not a search for the global minimum-volume box over
  every orientation (a materially more expensive, unattempted problem);
  for a shape whose principal axes already line up with its tightest
  orientation - any box is the simplest example - the two coincide
  exactly. Verified with a hand-derivable case built around the fact that
  a uniform-density box's centroidal moments satisfy `Ixx < Iyy < Izz`
  exactly when its own dimensions satisfy `Lx > Ly > Lz` (each successive
  difference is proportional to a positive difference of squares): a
  4x2x1 box's OBB comes back centered at its own true center with axes
  exactly world X/Y/Z (longest to shortest) and half-extents exactly
  `(2, 1, 0.5)`; the SAME box rotated 41 degrees about an arbitrary axis
  through its own center reproduces the identical center and half-extents
  in the same order, with axes exactly the world X/Y/Z axes carried
  through that same rotation (up to the sign ambiguity every eigenvector
  has) - proving the box's own shape, not its placement in world space,
  determines the answer; and every vertex of the rotated box is checked
  directly to lie within the returned box along all three axes.
- `Brep::GetTightBoundingBox()`: a real, silent-overestimate bug fixed,
  found while building `SplitDisjointPieces()` above (see this file's own
  earlier, narrower entry for this method for the history of what was
  already known before this). Root cause, confirmed by reading
  `ON_Brep::GetTightBoundingBox()`'s own source in full rather than
  inferring from behavior: it computes each face's contribution purely
  from that face's UNDERLYING SURFACE (vertices, a Greville-abscissa
  isocurve refinement, each face's own surface bbox) and NEVER consults
  that face's actual trim boundary at all, even when a real trim loop
  exists. Proven to be a general OpenNURBS behavior, not specific to any
  one factory here: `TrimmedPlanarFace()` lets a caller trim an
  arbitrarily small polygon out of an arbitrarily large surface directly,
  and the box came back sized to the WHOLE untrimmed surface, completely
  ignoring the trim - independently confirming `FromMixedFaces()`'s own
  5%-padded planar surfaces weren't a one-off coincidence either.
  Now exact for a face whose surface is a genuine, non-rational, bilinear
  (degree (1,1), 4 control points) surface with a ZERO "twist" term
  (`P00 - P10 - P01 + P11`, checked directly on the surface's own control
  points) - i.e. a true AFFINE map, exactly what
  `FromPlanarFaces()`/`FromMixedFaces()`/`TrimmedPlanarFace()` build for
  every planar face. Zero twist, not just flatness, is what's required:
  a merely planar-IMAGE bilinear patch (4 coplanar corners) can still
  curve a diagonal `(u, v)` line WITHIN that same plane if its twist is
  nonzero - confirmed with a concrete hand-built counterexample (4
  coplanar corners with nonzero twist; its own diagonal isocurve measured
  genuinely non-collinear via a cross product) before this was trusted,
  since using only a trim polygon's own discrete vertices for such a face
  could UNDERSHOOT the true tight box (a straight UV chord between two
  polygon vertices bows into a curve in 3D, and that curve's own bulge
  isn't necessarily bounded by its two endpoints' own straight-line box -
  though it IS always bounded by the WHOLE surface's 4-corner box, since
  every bilinear point is a convex combination of its corners regardless
  of twist; that weaker fact is what still keeps every OTHER face's
  fallback below always safe). Given zero twist, every straight edge of
  the face's own stored trim polygon (`face_trim_loops_`, straight-in-UV
  by that table's own convention) maps to a straight edge in 3D too, so
  the box of its own stored vertices (or, for an untrimmed such face, its
  own domain corners) IS the face's exact real boundary. Only trusted
  when that side table is genuinely in lockstep with this Brep's own
  `FaceCount()` (the same self-check `Compound()`/`SplitDisjointPieces()`
  apply). A second, independent pitfall found (via a direct probe, not assumed)
  and rejected while building this: the obvious-looking
  `ON_BrepFace::GetTightBoundingBox()` (inherited from
  `ON_SurfaceProxy`/`ON_Surface`) is a DIFFERENT, cruder algorithm than
  `ON_Brep::GetTightBoundingBox()`'s own inline per-face logic - probed
  directly on a doubly-curved bicubic bulge surface, it returned the raw
  control-point extent (a height of 1.0x the peak, not the documented
  0.5x overshoot), completely missing the Greville-abscissa isocurve
  refinement. Using it for every non-affine face's fallback would have
  silently LOOSENED this method's own already-tested behavior for every
  curved face - caught only by re-running the FULL existing test suite
  and finding `TestBrepGetTightBoundingBox`'s own pre-existing bicubic
  bulge check newly failing. The fix: build a throwaway single-face
  `ON_Brep` from that face's own surface and run the real
  `ON_Brep::GetTightBoundingBox()` on it - the SAME algorithm, scoped to
  one face (its own per-face loop has no cross-face dependency beyond a
  pure early-out optimization), confirmed by a direct probe to reproduce
  the documented bicubic-bulge overshoot value exactly, restoring
  bit-for-bit the same answer as before this fix for every non-affine
  face. Verified with the original repro (`FromPlanarFaces(Box(0,0,0,1,1,1))`
  now gives exactly `(0,0,0)`-`(1,1,1)`, not the padded
  `(-0.05,...)`-`(1.05,...)`), the general factory-independent repro
  (`TrimmedPlanarFace()`'s small-trim-on-big-surface now gives exactly
  the trim's own box, not the whole surface's), a direct regression guard
  (a curved cylindrical face's box is checked to contain a dense 98x98
  independent sampling of that same surface - never undershoots), and the
  full pre-existing test suite re-run to confirm `Box()`'s, `Sphere()`'s
  and the bicubic bulge's own already-established exact/overshoot values
  are all bit-for-bit unchanged. Verified boolean-adjacent: this Brep
  method underlies `BooleanCombinePlanar()`/`BooleanCombineMixed()`/
  `ShellConvexPlanar()`/`FilletConvexEdge()`'s own result construction, so
  the full 76-case general boolean sweep was also run before/after this
  change on this same branch (a shared sweep-baseline file across
  worktrees turned out to reflect a DIFFERENT session's own code, making
  a naive diff against it meaningless - the valid check is a stash-based
  before/after on one's own branch) and showed zero differences,
  confirming this Brep-level bounding-box query has no effect on the
  Manifold-mesh-based boolean pipeline at all.

- **`Brep::SphericalFace` + `FilletConvexEdges(solid, edges, radius)`:
  multi-edge constant-radius fillets with EXACT spherical vertex blends.**
  Until now the kernel could round one straight edge at a time and only
  close its ends against a perpendicular face with a flat corner notch;
  rounding a second edge of the same solid was rejected outright by
  `PlanarFaces()`, and the corner where three fillets meet - the shape a
  rolling ball actually leaves, a sphere octant - had no representation.
  Three additive pieces close that: (1) a fourth `FromMixedFaces` face
  kind, `SphericalFace` (frame, radius, longitude sweep, latitude range),
  built from `ON_Sphere::GetNurbForm` and trimmed in the sphere's own
  (u, v) with the same NURBS-parameter/radian conversion the cylinder
  path uses for u and the shifted-knot equivalent for v - both CHECKED by
  evaluating the real surface at every trim corner against the closed
  form; its pole side (the normal case for a vertex blend) becomes an
  `ON_Brep` SINGULAR trim, and `BuildFaceLoop` now takes an explicit
  per-segment table (`FaceTopology::segs`: isocurve direction, constant,
  parameter range, singular flag) that defaults to the legacy 4-point
  cylinder/cone rectangle so every existing face is built bit-for-bit
  as before. `MixedFaces()` hands the record back or, with no record,
  recovers frame/radius/angle/latitudes from `IsSphere` plus the
  surface's own quadrant points and the trim bounds. (2)
  `PlanarFace::notch_runs`: a face notched at several corners (a box end
  face under two parallel fillets) keeps one collapsed shared edge per
  notch instead of the second notch silently overwriting the first and
  leaving ~200 unshared micro-edges - found by checking `IsSolid()` on
  exactly that case. (3) `FilletConvexEdges`: every listed edge gets
  `FilletConvexEdge`'s own cylinder and rail re-trim (one half-space clip
  per filleted edge per face, so a face with two filleted edges meeting
  at a corner is inset to the single point `C + r*n_f`); at a trihedral
  vertex with all three edges filleted the ball center `C` is the 3x3
  solve `n_f.(C - V) = -r`, checked to lie on all three fillet axes, the
  three cylinders are SET BACK to the planes through `C` so their caps
  are great circles of the corner sphere, and the octant/spherical
  triangle is a `SphericalFace` whose equator arc is parameterized
  IDENTICALLY to the equator cylinder's cap (same xaxis, same
  orientation) so `FromMixedFaces`' arc-identity check welds them as one
  edge; the two meridians are quadrants, symmetric under reversal. One
  face of the corner must be perpendicular to the other two (every box
  corner, every prism corner with perpendicular caps - any side dihedral);
  a general tetrahedron corner, two fillets meeting where the third edge
  stays sharp, and valence > 3 vertices throw with the reason. Also fixed
  in passing: `Mesh::MergeAndWeld` now drops faces whose corners welded
  to one vertex (the zero-area triangles every grid tessellation emits
  along a sphere's pole row), so a welded `Brep::Sphere` finally reports
  `IsClosedManifold()` - it was watertight but failed the manifold check
  before, confirmed by reverting only that change. Verified
  (`TestSphericalFace*`, `TestFilletConvexEdges*`, `TestMergeAndWeld*`):
  the unit box with ALL 12 edges filleted at r = 0.2 is a 26-face, 48-edge,
  24-vertex `IsValid()`/`IsManifold()`-closed/`IsSolid()` Brep whose
  adaptive-tessellated volume converges from below to Steiner's formula
  `(1-2r)^3 + 6(1-2r)^2 r + 3(1-2r) pi r^2 + 4/3 pi r^3 = 0.907704993`
  (errors 1.7e-5, 1.8e-6, 1.9e-7 at chord tolerances 1e-5, 1e-6, 1e-7 -
  linear in the tolerance, i.e. pure chordal deficit); a regular
  hexagonal prism with all 18 edges filleted (60-degree equator sweeps)
  matches the same Steiner formula to 3e-7 at 1e-7; one rounded corner
  matches `1 - 3(1-r) r^2 (1-pi/4) - r^3 (1-pi/6)`; two parallel fillets
  are a closed solid with both end faces double-notched; a single-edge
  call reproduces `FilletConvexEdge`'s volume to 1e-12; the record round
  trip rebuilds the rounded box as a 26-face solid; and every unsupported
  configuration above is rejected. Honestly still open: a rounded solid's
  `TessellateToClosedMesh()` is NOT yet a closed manifold mesh - not
  because of the spheres (their seams with the cylinders coincide sample
  for sample at u_divisions = 2*v_divisions) but because every exact-clip
  PLANAR face adds boundary vertices where its own grid lines cross its
  polygon, T-junctions the adjacent cylinder rail does not share; this
  pre-dates this entry (a single `FilletConvexEdge` box has the same
  gap), `TessellateConforming()` does not yet close it either, and it is
  the next tessellation increment. General spherical-triangle corners,
  mixed radii, and concave edges remain out of scope as documented in
  `fillet.h`.

- **`FilletConvexEdge`'s OBLIQUE end condition, now closed** - the one
  disclosed gap its own doc comment named plainly: a third face at
  edge_p0/edge_p1 that is NOT perpendicular to the edge used to be left
  untouched (a genuinely free boundary there, or a topologically-invalid
  result whenever that third face was actually load-bearing). Where the
  perpendicular case's own cap is a plain circle (NotchCornerAtVertex),
  an oblique face cuts the fillet's circular CYLINDER in a true ELLIPSE -
  closed here by REUSING, not re-deriving, `detail/ellipse_clip3d.h`'s
  own `ComputeEllipseFrame3d`/`EllipsePointAt` (already exact and
  exercised for exactly this: an oblique plane's true intersection with a
  circular cylinder, from `BooleanCombineMixed`'s own oblique
  plane+cylinder case). The genuinely new geometry:
  `FindObliqueThirdFaceCrossing` (fillet.cpp) solves where each of the
  fillet's two straight rail lines crosses the oblique face's plane - a
  single linear equation per rail, `t = -(D.n_f)/(e.n_f)` where `D =
  radius*n_i - bis*offset` is the SAME fixed offset `contact_i`/
  `contact_j` already add to a point on the edge (getting this exactly
  right mattered: an earlier draft used the simpler-looking but WRONG `D
  = radius*n_i`, which silently mislocated every oblique crossing by the
  bisector offset - caught by cross-checking a hand-picked point against
  the oblique plane's own equation directly, not merely trusted, before
  it reached a test). The two rail crossings generally land at TWO
  DIFFERENT heights along the edge (unlike the perpendicular case, where
  both sit at the vertex's own height) - the i-side one becomes the
  cylinder's own new v=0 or v=length reference (shifting frame.origin/
  length so it's exactly the flat corner
  `Brep::CylindricalFace::cap0_notch_points`' own contract already
  requires), the j-side one becomes that cap's own genuinely SLOPED back
  point - precisely the "sloped cut chain" shape that field's own doc
  comment already anticipated for an unrelated producer (the unequal-
  radius cylinder/cylinder split), just reached here from the fillet's
  own end condition instead. `EllipseNotchCornerAtVertexCylindrical`
  splices the identical dense ellipse sample into both the third face's
  own notch and the cylinder's own `cap0_notch_points`/
  `cap1_notch_points` - a literal shared boundary curve, mirroring
  `FilletConvexEdgeTapered`'s own already-established principle for its
  cone case. Every rail crossing is also checked to land within the third
  face's own real extent (mirroring `ChamferConvexEdge`'s own
  `ChamferEndAtVertex` overrun check), throwing rather than silently
  building a corner past where the geometry actually has material. When
  no third face is oblique at either end (v0_start == 0, v1_end == L
  exactly), the whole construction is an inert no-op - verified
  bit-identical to before, not merely argued: see
  `TestFilletConvexEdgeObliqueEndMatchesPerpendicularAtZeroSlope`.
  Verified (`TestFilletConvexEdgeObliqueEnd*` in test_basic.cpp): a
  hexahedron whose +x end face is the oblique plane `x = 1 + slope*y`
  (the same fixture family `ChamferConvexEdge`'s own oblique test uses)
  fillets to an `IsValid()`/`IsManifold()`-closed/`IsSolid()` Brep whose
  two rail/oblique-plane crossing vertices land exactly at the hand-
  derived points `(1 + slope*r, r, 1)` and `(1, 0, 1 - r)`, whose cylinder
  length is shifted to `1 + slope*r`, and whose volume matches an
  independently-derived closed form - the removed wedge's own constant
  cross-sectional area times the affine "height along the edge" function
  evaluated at the wedge's own centroid (the standard fact that an affine
  function integrates over a region as its own value at that region's
  centroid times the area) - to within 5e-6, checked at TWO independent
  (slope, radius) pairs; a degenerately-perpendicular "oblique" end
  (slope = 0) reproduces a plain box fillet's face/edge/vertex counts and
  volume bit-for-bit; and an oversized radius on the oblique fixture is
  still rejected. Full `dino8_kernel_smoke`: 2147 checks, 0 failures.
  `dino8_general_boolean_sweep` output is byte-identical to the baseline.
  Honestly still open: the dedicated overrun check
  (`FindObliqueThirdFaceCrossing`'s own third-face-extent test) has no
  test that isolates it from the pre-existing, unrelated "radius exceeds
  a face's own extent" check - both happen to coincide for every simple
  quadrilateral-faced fixture this increment's own tests use, and
  distinguishing them would need a non-quadrilateral face i shape, left
  for a future increment rather than staged to look tested when it isn't.
  A solid already carrying a curved face (from an earlier fillet or a
  `FilletConvexEdges` corner) remains out of scope, since `PlanarFaces()`
  itself rejects it - closing that is this subsystem's own next gap.

- **`RemoveBlend(solid, point_on_fillet)`** - blend removal, the
  Parasolid "blend/chamfer" class's own last standard operation: restores
  the sharp edge a `FilletConvexEdge` call rounded off, working PURELY
  from the filleted solid's own geometry (a `Brep` carries no separate
  operation history anywhere, so this genuinely RECOVERS the original
  shape rather than replaying a recorded step - the same "read it back
  out of the geometry" spirit `MixedFaces()` already uses to recognize a
  `CylindricalFace`/`ConicalFace`/`SphericalFace`). Construction: the
  cylindrical face nearest `point_on_fillet` is identified (measured
  against its own trimmed extent, not the infinite cylinder); its two
  adjacent planar faces are found by matching their own loop against the
  cylinder's two straight rails; the restored sharp edge follows directly
  from the algebraic inverse of `FilletConvexEdge`'s own `axis_point`
  construction (`edge_p0 = frame.origin + bis*offset`, recomputing
  `bis`/`cosb`/`offset` from the two recovered face normals and the
  cylinder's own radius); each adjacent face's rail edge is spliced back
  to the sharp edge; and any third face's own corner-notch (a dense
  polygonal run between the same two rail corners - `NotchCornerAtVertex`'s
  own construction) is found and collapsed back to the single restored
  vertex - the genuine inverse splice, `CollapseNotchRun`. Before
  trusting any of this, each end is checked against every `SphericalFace`
  of `solid`: a `FilletConvexEdges` corner cylinder is set back so its own
  end rail corners sit exactly on a corner sphere's own surface, and an
  end matching that pattern throws rather than silently restoring the
  wrong shape (a plain `m == 1` end - whichever function built it - is
  unaffected, since the math is identical either way); a cylinder with a
  non-empty `cap0_notch_points`/`cap1_notch_points` (an oblique-end
  fillet's own sloped ellipse cap) is rejected the same way.
  A GENUINE BUG was caught and fixed while building this, not merely
  disclosed after the fact: `CollapseNotchRun`'s first draft passed a
  single-fillet round trip cleanly but silently corrupted the case where
  a face carries TWO separate notch runs (two parallel fillets sharing an
  end face, `PlanarFace::notch_runs`) - the point-removal itself
  ROTATED the loop (starting the new array at the collapsed run's own far
  end and appending the restored vertex last) while the notch-metadata
  shift assumed a plain in-place erase-and-insert, so the OTHER,
  untouched notch's own recorded indices silently pointed at the wrong
  loop positions after the rebuild - producing a `Brep` that still
  reported `IsValid()` and `IsManifold()` returning true/oriented but
  `has_boundary` ALSO true (a real naked edge), caught by an explicit
  regression for exactly that two-notch scenario, not by the simpler
  round trip alone. Fixed by making the point removal a genuine in-place
  erase-and-insert matching the metadata math exactly.
  Verified (`TestRemoveBlend*` in test_basic.cpp): a single fillet on a
  unit box round-trips to a 6-face/12-edge/8-vertex solid with the
  original box's own volume to floating-point precision and every one of
  its 8 corner vertices restored exactly; removing one of two parallel
  `FilletConvexEdges` fillets sharing double-notched end faces leaves a
  closed, valid, solid `Brep` with exactly the OTHER fillet's own volume
  remaining (not both, and not a naked boundary); and a solid with no
  cylindrical face, a point far from every cylindrical face, a
  spherical-corner cylinder, and an oblique-end cylinder are all
  rejected. Full `dino8_kernel_smoke`: 2397 checks, 0 failures.
  `dino8_general_boolean_sweep` output is unchanged. Honestly still open:
  `FilletConvexEdgeTapered`'s own `ConicalFace` and `FilletConvexEdges`'
  own spherical corners remain out of scope, disclosed rather than
  approximated - the natural next increment for this one function.
- `NurbsSurface::UnrollDevelopable(u_divisions, v_divisions, out_flat,
  &area, &kind)`: unrolls a plane, cylinder, or cone - the only shapes
  an ON_NurbsSurface can be that are actually developable (zero
  Gaussian curvature everywhere, the classical differential-geometry
  condition for "unrolls to the plane without distortion") - into a
  flat `Mesh`, using OpenNURBS' own `IsPlanar`/`IsCylinder`/`IsCone`
  (real geometric fits, tried in that order) to detect which. This is
  the exact complement to dino8-app's existing Unroll/Squish/Smash
  commands, which use a from-scratch triangulation-based distance-
  preserving heuristic that works on *any* surface but is never exact,
  even for a perfect cylinder (it reports a measured "distortion"
  percentage). Every flat vertex here is placed by mapping the true 3D
  point directly through the matched primitive's own closed-form
  inverse - `ON_Cylinder`/`ON_Cone::ClosestPointTo()` give the exact
  (angle, height) of a point already known (within tolerance) to lie on
  that primitive, converted to flat `(radius * angle, height)` for a
  cylinder or, for a cone, to the exact slant distance from the apex
  `L = height / cos(halfAngle)` at unrolled angle `angle *
  sin(halfAngle)` (the classical cone-unroll construction: a full lap's
  true circumference `2*pi*L*sin(halfAngle)` becomes a flat sector of
  radius L spanning that many radians, which has the same arc length by
  construction) - not by integrating or accumulating edge lengths
  across the mesh.
  A real bug found and fixed before finalizing: `ClosestPointTo()`'s
  angular parameter wraps at the atan2 branch cut, so a naive per-
  vertex lookup tears a genuine full-360-degree loop apart at the seam
  (one column jumps back by 2*pi instead of continuing) - confirmed by
  a debug run on a real closed `ON_Cylinder::GetNurbForm()` wall, fixed
  with a standard phase-unwrap pass (detect which parametric direction
  is the primitive's own circular one via `IsClosed()`, then walk it
  making each step continuous) before any point is turned into a flat
  coordinate. Verified with real per-point geometry, not just "didn't
  crash": on a partial (270-degree) cylinder, every flat vertex at the
  same height sits at *exactly* the same flat y regardless of angle,
  the total flat height span is exactly the true cylinder height, and
  the total flat circumferential span is exactly `radius * sweptAngle`
  - all to `ON_Mesh`'s own single-precision vertex storage limit, not a
  convergent approximation; the same circumference check on a genuine
  *full-loop* wall confirms the unwrap fix actually works (span is
  exactly `radius * 2*pi`, monotonically increasing, not torn at the
  seam). On a cone, every sampled vertex's true 3D distance from the
  apex exactly equals its flat distance from the unrolled apex (origin),
  the base rim sits at exactly the true slant length, and the full
  loop's total unrolled angle is exactly `2*pi*sin(halfAngle)`. On a
  tilted planar quad (a genuine rigid-body isometry, no chord-vs-arc gap
  at all), *arbitrary* pairwise 3D distances - not just adjacent-sample
  ones - equal their flat counterparts exactly. `out_area` (the flat
  mesh's own measured area) is checked against the true closed-form
  patch area: exact (to any division count, even a deliberately coarse
  4x1 grid) for a cylinder - proven why in the test's own comment, a
  cylinder's flat map makes every quad cell an exact axis-aligned
  rectangle regardless of how non-uniformly the source NURBS parameter
  is spaced in angle - and within 5% for a cone (which really is only a
  tessellation approximation, since a flat cone-sector cell is a wedge,
  not a rectangle). Refused (`Result::Failed`, `out_flat` untouched) for
  a genuine sphere and for a generic freeform wiggly bicubic - neither
  developable, confirmed rather than assumed by testing both. A
  mutation (disabling the angle-unwrap pass) makes exactly the 5 checks
  that depend on it fail, including the full-loop seam check, closing
  the loop on why that fix was needed rather than just asserting it.
- **`NurbsSurface::OffsetAnalytic(distance, out, tolerance)`** (2026-09-24)
  - the kernel's first surface-offset capability at all (Parasolid
  `PK_BODY_offset`'s per-face case): before this, nothing in the kernel
  could compute *any* surface offset, exact or approximate. Scoped
  deliberately to the five analytic types whose true offset (the actual
  locus of points at `distance` along the normal, not a refit
  approximation of it) is ITSELF expressible in exactly the same closed
  form, detected the same real, tolerance-based way `UnrollDevelopable()`
  already does (`IsPlanar`/`IsSphere`/`IsCylinder`/`IsCone`/`IsTorus`, not
  a name check): a plane offsets to a translated plane; a sphere/cylinder
  to a concentric/coaxial one of radius `radius +/- distance`; a torus to
  a coaxial one with the SAME major radius and tube radius `minor_radius
  +/- distance`. The cone case is the interesting one, and is exact for a
  genuinely non-obvious reason, not assumed: parametrizing the cone as
  `P(h, theta) = (h*tan(alpha)*cos(theta), h*tan(alpha)*sin(theta), h)`
  and solving for the one apex shift `z_a` that makes
  `P(h, theta) + distance * outward_normal` land exactly on a
  same-half-angle cone with apex at `z_a` gives the closed form
  `z_a = -distance / sin(alpha)`, independent of `h` and `theta` - i.e.
  a cone's offset really is another cone with the SAME half-angle, apex
  shifted along the axis, everywhere on the surface, not just near one
  checked point (confirmed in the test to 3e-14 via a golden-section
  search matching an arbitrary sampled point's own `point + distance *
  normal` against the constructed offset surface, not merely trusted
  from the algebra). A genuinely freeform (non-analytic) surface's true
  offset is generally not an exact NURBS surface at all - that harder,
  inherently-approximate case is deliberately refused
  (`Result::Failed`) rather than silently approximated, matching this
  file's own honesty standard elsewhere (`UnrollDevelopable`'s "refused,
  not distorted" for a non-developable surface).
  A real, easy-to-get-wrong wrinkle found and fixed before finalizing:
  the sign of "along the normal" can't be assumed fixed, since nothing
  guarantees a given `NurbsSurface`'s own `du x dv` handedness agrees
  with the geometrically "outward" direction (this file's own
  `IsSphere()` doc comment already found `EvNormal`'s sign surprising in
  exactly this way for one shape) - so the sign is resolved AT RUNTIME
  per call, by comparing `NormalAt()` at a sample point against the
  fitted primitive's independently-known true outward direction there
  (e.g. `point - sphere.Center()`), rather than hard-coded. A second real
  bug, caught only by testing (not by reading the derivation): the
  natural-seeming `cylinder.circle.plane.ClosestPointTo(point)` does NOT
  give the closest point on the cylinder's AXIS - it projects onto the
  circle's own 2D cross-section plane, dropping only the along-axis
  component, so it returns a point still `radius` away from the true
  axis whenever the sample sits at a different height than that plane's
  own origin (caught because the test's sample height happened to
  coincide with the fitted cross-section's own height, making the bug
  read as `ClosestPointTo(p) == p` exactly - too clean a result to be
  right). Fixed by projecting onto the axis LINE directly
  (`origin + dot(p - origin, axis) * axis`), not the plane. A third real
  finding, structural rather than a bug: `ON_Surface::IsCylinder()`'s own
  fallback fit (the only path that ever runs here, since a bare
  `ON_NurbsSurface` never casts to `ON_RevSurface`) never recovers the
  surface's actual finite height extent - it leaves `cylinder.height[0]
  == height[1] == 0`, OpenNURBS' own "infinite cylinder" encoding, whose
  `GetNurbForm()` always fails - so the real extent has to be recovered
  independently here from this surface's own v-domain ends before an
  offset cylinder can be built at all.
  Also enforces the real self-intersection hazard Parasolid's own offset
  is documented to guard against, rather than silently building an
  invalid or self-overlapping surface: refused when a sphere/cylinder's
  radius or a torus's tube radius would go `<= 0` (the offset exceeds
  that constant-curvature surface's own radius of curvature), when a
  torus's tube radius would reach or exceed its major radius (a
  self-intersecting spindle torus, a materially different degenerate
  shape from merely "too fat", not the same check restated), and, for a
  cone, when the offset shrinks the radius through zero anywhere within
  the surface's own existing v-domain (checked at both domain ends,
  where a cone's monotonic radius is smallest) - the cone's own
  analogue of "offset exceeds local radius of curvature", since a
  cone's circumferential radius of curvature at a point is exactly its
  distance from the axis there. The plane case is handled differently
  from the other four, and deliberately better: rather than rebuilding a
  primitive's natural full extent via `GetNurbForm()` (a plane has no
  such single bounded natural form to rebuild - a plane itself is
  unbounded), it translates THIS surface's own existing control points
  by `distance * normal` directly (weight-preserving, via the
  homogeneous `ON_4dPoint` CV form, not the Euclidean `SetCV()` overload
  that resets a rational control point's weight to 1 as a documented
  side effect elsewhere in this file) - exact for ANY planar surface
  regardless of its actual shape, and the only one of the five cases
  that preserves the original surface's exact domain, control-point
  count, and trim compatibility, confirmed directly in the test (domain
  and CV-grid size checked equal before/after, not just claimed).
  Verified in `tests/test_basic.cpp`
  (`TestSurfaceOffsetAnalytic*`): exact concentric/coaxial radius checks
  for sphere/cylinder/torus; half-angle preservation plus a
  golden-section-search pointwise match (3e-14) for the cone; exact
  translation-by-normal and domain/CV-count preservation for the plane;
  all four self-intersection refusals (sphere/cylinder collapse,
  spindle torus, cone through-axis fold); refusal on a genuine freeform
  bulge surface; and the `distance == 0` no-op case. Confirmed the tests
  actually exercise this code, not merely compile around it: reverting
  just `surface.h`/`surface.cpp` (`git stash`) makes every
  `OffsetAnalytic` test a compile error, not a runtime failure - the
  method genuinely did not exist before this entry.
  Deliberately out of scope at the time, disclosed rather than silently
  missing: curve offset (closed the same day, see below), body/solid
  (Minkowski-style) offset, shell/hollow beyond the existing
  `ShellConvexPlanar`, per-face wall-thickness overrides, and
  thicken-sheet-to-solid - the last four still aren't implemented.
- **`NurbsCurve::OffsetInPlane(distance, out, tolerance)`** (2026-09-24) -
  the curve-level counterpart to `OffsetAnalytic()` above, and, like it,
  the kernel's first curve-offset capability at all. Same honesty split:
  EXACT for a line (translated parallel) and a circular arc/full circle
  (concentric, same plane/center/`DomainRadians()` angular span, radius
  `radius +/- distance`), an explicitly-APPROXIMATE least-squares refit
  (this class's own real `FitLeastSquares()`) for every other planar
  curve, and outright refusal (`Result::Failed`) for a non-planar curve
  or a distance that would fold the curve through itself - never a
  silent approximation dressed up as exact, and never a silently
  self-intersecting result. The offset direction at parameter `t` is
  `TangentAt(t) x plane.zaxis` (`plane` from this curve's own
  `IsPlanar()` fit); for the arc case specifically, the +/- sign is
  resolved the SAME way `OffsetAnalytic()` resolves it for a sphere/
  cylinder/torus - at runtime, by comparing that direction against the
  independently-known true outward radial `point - center` at one
  sample, not trusted from `ON_Arc`'s own parametrization convention -
  so `distance > 0` reliably GROWS an arc/circle regardless of which way
  a particular curve's tangent happens to wind (deliberately not
  assumed fixed, the same reasoning already documented for the surface
  case). For a general curve there is no such independently-known
  "outward" to check against, so that curve's own `IsPlanar()`-fitted
  zaxis sign is used as-is - a real, disclosed asymmetry with the arc
  case, not an oversight.
  Self-intersection is checked in the general (approximate) path by
  comparing, AT EVERY SAMPLE, the signed component of `distance` toward
  that point's own `CurvatureAt(t)` center against that point's own
  local radius `1/kappa`: reaching or exceeding it means the offset
  folds the curve through itself there - the direct curve analogue of
  `OffsetAnalytic()`'s cone guard, and the real hazard a naive per-point
  translate-and-refit would otherwise hide silently in a plausible-
  looking but self-overlapping result. Verified in
  `tests/test_basic.cpp` (`TestCurveOffsetInPlane*`): exact
  radius/length checks for the line, circle, and a partial (half-circle)
  arc (the arc case additionally checked to preserve its exact start
  angle, not just its radius); a smoothly-curved general cubic's offset
  matches a direct per-point `point + distance*normal` construction to
  within a loose but meaningful tolerance (worst case ~0.004 units on a
  10-unit curve, nowhere near the 0.05 threshold - not tuned to just
  barely pass); both self-intersection guards (an oversized circle
  offset and, on the general curve, at least one of a large positive or
  negative offset folding through a tight bend); refusal on a genuine
  non-planar curve; and the `distance == 0` no-op case. Confirmed by the
  same stash-based method as `OffsetAnalytic()`'s own entry: reverting
  just `curve.h`/`curve.cpp` turns every `OffsetInPlane` test into a
  compile error, not a runtime failure.
  Still deliberately out of scope: a genuinely non-planar 3D curve
  offset (e.g. sweeping a Frenet frame along the curve), body/solid
  offset, shell/hollow beyond `ShellConvexPlanar`, per-face wall-
  thickness overrides (closed the same day, see below), and
  thicken-sheet-to-solid.
- **`ShellConvexPlanar(solid, removed_faces, wall_thickness)`**
  (2026-09-24) - the per-face wall-thickness overload (Parasolid
  `PK_BODY_shell`'s own per-face `thickness` array, as distinct from its
  single-scalar form) of the existing scalar `ShellConvexPlanar(solid,
  removed_faces, t)`. Not a second implementation: the scalar form is now
  a one-line delegation (`wall_thickness` filled uniformly with `t`), so
  its own already-verified behavior - every existing degeneracy/adjacency
  check included - is provably unchanged, confirmed directly in the test
  (the per-face overload with every entry equal reproduces the scalar
  overload's own exact volume, not just approximately). The actual
  generalization is small and mechanical: every place the single scalar
  `t` used to offset a KEPT face's own plane/loop inward, this uses THAT
  FACE's own `wall_thickness[i]` instead - the surrounding machinery
  (each face's inner offset independently clipped against every OTHER,
  possibly differently-offset, face's own constraint plane; the rim/
  washer construction around each opening) needed no change at all, since
  it already worked in terms of each face's own already-computed
  constraint plane `pi[i]`, never the scalar `t` directly, once `pi[]`
  itself is built from per-face values.
  Verified against a genuine EXACT closed-form generalization of the
  existing scalar test's own cube formula, not merely spot-checked: for
  an open-top cube (removing the one non-axis-paired face), giving each
  of the five KEPT faces its own distinct thickness gives cavity_volume
  = `(s - t_left - t_right) * (s - t_front - t_back) * (s - t_bottom)`
  - the direct per-axis generalization of the scalar case's own
  `(s-2t)^2*(s-t)` (which is just this formula with every `t_*` equal) -
  confirmed to match `ShellConvexPlanar`'s own exact
  (double-precision, untessellated) volume to 1e-9 for five genuinely
  different thickness values, not a uniform or symmetric case that could
  hide an indexing bug. Also checked: the result keeps the same 14-face
  topology and closed/watertight tessellation as the uniform case;
  `wall_thickness.size()` mismatched against `PlanarFaces().size()`
  throws; a non-positive entry on a KEPT face throws; and a REMOVED
  face's own entry (which bounds no wall of its own) is never read, even
  when set to a nonsensical negative value - confirmed by testing rather
  than assumed, since a careless implementation could easily validate
  every entry unconditionally. Confirmed by the same stash-based method
  the other entries here use: reverting just `boolean.h`/`boolean.cpp`
  turns every new per-face test into a compile error. The general
  boolean sweep (`dino8_general_boolean_sweep`) is byte-for-byte
  identical before and after this change, as expected for a change that
  never touches `boolean_general.cpp`.
  Still deliberately out of scope: extending `ShellConvexPlanar` itself
  to non-convex or curved-face solids (both throw, unchanged - see
  `ShellConvexPlanar`'s own doc comment for exactly which precondition
  fires and why: a non-convex solid would clip pieces of itself away
  against its own offset planes, and this function operates on
  `PlanarFaces()` alone, so a curved-face Brep isn't representable
  here at all - a genuine curved-face shell is a substantially larger
  undertaking, on the order of `BooleanCombineGeneral` itself, not
  attempted in this pass), thicken-sheet-to-solid (closed for the
  closed-surface case the same day, see below), and body/solid offset.
- **`ShellClosedSphere(center, outer_radius, thickness)` /
  `ShellClosedTorus(plane, major_radius, outer_minor_radius,
  thickness)`** (2026-09-24) - the closed-surface counterpart of
  `ShellConvexPlanar()` above, for the one case that function cannot
  reach AT ALL: a sphere or torus has no planar faces for
  `PlanarFaces()` to see, so `ShellConvexPlanar()` cannot even be
  CALLED on one, let alone shell it. A full sphere/torus is already a
  closed 2-manifold with no boundary curve, so unlike
  `ShellConvexPlanar()`'s own planar rim washers (needed to close the
  gap where a face was removed), NO wall/rim construction is needed
  here at all: the whole thing is just a concentric inner copy (radius
  `outer_radius - thickness`, or minor radius `outer_minor_radius -
  thickness` at the SAME major radius/plane for the torus) with its
  single face reversed (`ON_Brep::FlipFace`, so its own outward-from-
  material direction points INWARD, toward the cavity), combined with
  the outer copy via the existing, already-documented-and-tested
  `Brep::Compound()` - which exists for exactly this "N disjoint closed
  shells make one solid" case (its own doc comment: "IsValid()/
  IsSolid() hold for a compound of valid solid lumps ... Tessellate*()
  volumes add up per face"), so this needed no new Brep-level topology
  machinery at all, only composing three already-public APIs
  (`Brep::Sphere()`/`ON_Torus::GetNurbForm()` via `Brep::FromSurface()`,
  `FlipFace()`, `Compound()`).
  A real thing checked before trusting this, not assumed safe: a
  Sphere()-/FromSurface()-built face (the "minimal NewFace(surface_
  index)-only path" brep.h's own class comment already flags) reports
  `raw().IsValid()`/`IsSolid()`/`IsManifold()` all false REGARDLESS of
  this change - confirmed directly by checking a bare `Brep::Sphere()`
  alone shows the exact same false/false/false, so this is a
  pre-existing, already-disclosed limitation of that construction style
  (no real ON_Brep loop/trim/edge topology), not something this
  introduces or makes worse; the correctness signal this kernel already
  relies on for such Breps - the TESSELLATED mesh's own
  `IsClosedManifold()`/`Volume()` - is what's actually checked here, and
  it comes back genuinely closed and watertight.
  Verified against the exact closed-form shell volumes: a sphere shell
  matches `4/3*pi*(R^3-(R-t)^3)` and a torus shell matches
  `2*pi^2*R*(r^2-(r-t)^2)` (Pappus's theorem applied to the outer minus
  inner solid tori, both at the same major radius R - exactly what
  `NurbsSurface::OffsetAnalytic()`'s own torus case already established:
  offsetting a torus changes only the minor radius), both within the
  tessellation's own density-limited tolerance; `LumpFaceRanges()`
  reports two separate lumps, not a single welded 4-face shell (the
  outer and inner spheres/tori share no topology at all, correctly);
  and both functions refuse a thickness at or beyond the applicable
  radius (collapsing/inverting the inner copy through the center) and,
  for the torus, an outer minor radius at or beyond the major radius (a
  self-intersecting spindle torus) - the same self-intersection hazards
  `NurbsSurface::OffsetAnalytic()`'s own sphere/torus cases already
  guard against, checked here independently since this doesn't
  delegate to `OffsetAnalytic()` (it rebuilds the inner primitive
  directly, the same way `Brep::Sphere()` itself does, rather than
  offsetting an existing surface).
  Still deliberately out of scope: the general TRIMMED (non-closed)
  analytic patch case - thickening a spherical/cylindrical/conical
  wedge or cap that has real boundary curves needs actual wall/rim
  construction (this kernel's `SphericalFace`/`CylindricalFace`/
  `ConicalFace` blend-patch structs support trimming, but connecting a
  trimmed patch's boundary to its offset counterpart is a genuinely
  larger undertaking than this pass attempts, even though the connecting
  walls turn out to be exact cones/planes for a sphere - a real,
  disclosed follow-on, not attempted here); a full untrimmed cylinder or
  cone shell (both have circular end boundaries needing their own caps,
  unlike the sphere/torus's total closure); and body/solid offset.
- `NurbsSurface::CoonsPatch(bottom, top, left, right, out, tolerance,
  &out_corner_gap)`: the exact bilinearly-blended Coons patch through 4
  boundary curves (Parasolid/Rhino's NetworkSrf/EdgeSrf for exactly 4
  curves), as real NURBS control-point algebra - the classical
  `S = R_uv + R_vu - B` construction (a ruled surface between
  `bottom`/`top`, a ruled surface between `left`/`right`, minus a
  bilinear correction through the 4 corners), all three brought to one
  shared (degree, knot vector) pair in both directions via
  `ElevateDegree()`/`InsertKnotAt()` (both already-tested,
  shape-preserving) so the sum is exact homogeneous control-point
  arithmetic, never a fit. This directly replaces a real weaker
  approximation in dino8-app's own existing `NetworkSrf` command for
  its 4-curve case: that command's `SurfaceFromRows()` samples each
  curve into discrete points and hands them to `FromControlGrid()`,
  which treats sampled points *as* control points - and a B-spline
  generally does not pass through its own control points, so that
  surface's boundary only approximates the source curves (measured in
  the tests: > 1e-3 off on a genuinely curved boundary, vs. this
  method's < 1e-9).
  `bottom`/`top` and `left`/`right` are auto-oriented (each of `top`/
  `right` tried both as given and reversed, 4 combinations, whichever
  best closes all 4 corners) since a caller chaining arbitrarily-picked
  curves - the real situation an app command using this is in - can't
  otherwise guarantee a consistent winding.
  Two real bugs found and fixed while building this, both confirmed by
  a debug run before assuming a cause, not guessed at: (1)
  `FromControlGrid()`'s own doc comment claimed "u varies fastest" for
  its `control_grid` indexing; the actual code is `idx = u * v_count +
  v` (v varies fastest) - a doc-only fix (see there), but it broke this
  method's own bilinear-correction-term construction until traced with
  a scratch probe. (2) `ON_NurbsCurve::Reverse()` (already documented,
  correctly, on `NurbsCurve::Reverse()`'s own doc comment as not
  preserving the prior domain) needs its domain re-normalized
  immediately after reversing for this method's own orientation search
  to compare endpoints meaningfully - missing that made every
  "reversed" trial candidate compare against the wrong, un-normalized
  parameter range, confirmed by a debug run showing `PointAt(0)`/
  `PointAt(1)` landing on the wrong (in one case a domain-negated,
  off-curve) points after a raw `Reverse()`.
  Verified with real control-point-level geometry, not sampled fitting:
  on 4 genuinely different curved boundaries (cubic, not straight
  lines, so a coincidental match is not possible), the built patch's
  own 4 boundary isocurves reproduce all 4 original input curves
  exactly (< 1e-9 at 41 samples each) and all 4 corners exactly; the
  same patch built from `top`/`right` handed in pre-reversed is
  geometrically identical (< 1e-9) to the correctly-oriented build,
  confirming the auto-orientation search; 4 curves that never actually
  meet are refused with a genuinely large (not rounding-level) reported
  corner gap. A mutation (dropping the bilinear correction term) makes
  the method's own internal self-check catch the wrong result and fail
  closed, which the corresponding test then observes.

- **`RemoveBlend` extended to `ConicalFace`** - closes the first of the
  two gaps that increment's own README entry disclosed: a
  `FilletConvexEdgeTapered`-built taper (or one segment of an N-station
  one) can now be removed the same way a plain constant-radius fillet
  can. The inverse is genuinely simpler than inverting the taper's own
  apex/axis construction directly: the rolling-ball radii at the
  segment's own two ends follow in closed form from the cone's own TRUE
  radii (`r_lo = radius0/c`, `r_hi = radius1/c`, `c = 1/sqrt(1 +
  tan_half_angle^2)`, with `tan_half_angle` already computable from the
  cone's own `radius0`/`radius1`/`length` alone), and the cone's own rail
  corner at `(v0, angle 0)` is EXACTLY `edge_p0 + r_lo*k_i`
  (`FilletConvexEdgeTapered`'s own `rail_i(0)`, `k_i = n_i - bis/cosb` a
  fixed vector once the two adjacent face normals are recovered) - so
  `edge_p0`/`edge_p1` fall out by subtraction, with NO separate recovery
  of `m`/`Umag` ever needed. The SAME two points reconstructed
  independently from face j's own `k_j` (a genuinely different vector
  from `k_i`, so this is a real, discriminating checked invariant) must
  agree, or the call throws rather than restoring the wrong shape. A
  notched tapered end needs no separate oblique-rejection branch the way
  the cylindrical case does: every `ConicalFace` corner-notch already
  uses the dense-ellipse-run construction regardless of the third face's
  own orientation, and `CollapseNotchRun` works purely by matching 3D
  points, agnostic to which curve family produced the run. `RemoveBlend`
  now checks a point against both `MixedFaces().cylindrical` and
  `.conical` and removes whichever patch is actually closer.
  Verified (`TestRemoveBlendRoundTripsATaperedFillet`): a box edge
  tapered-filleted 0.2->0.35 (both ends perpendicular, so BOTH get the
  ellipse corner-notch `FilletConvexEdgeTapered`'s own v1 gap-closing
  built) round-trips to the exact 6-face/12-edge/8-vertex unit box, valid/
  manifold/closed/solid, volume matching to floating-point precision,
  every corner restored. Full `dino8_kernel_smoke`: 2409 checks, 0
  failures; `dino8_general_boolean_sweep` unchanged. Honestly still open:
  `FilletConvexEdges`' own spherical vertex-blend corners remain the one
  disclosed gap left - no existing kernel entry point can even BUILD a
  solid carrying both a cylindrical and a conical fillet in one call, so
  the cylindrical-vs-conical distance comparison this increment adds is
  exercised only by each type's own single-fillet fixture, not by a
  genuine two-type one; noted plainly in the test file rather than staged
  to look covered.

## What's still not done (as of chunk 2)

- `Brep::Box()`, `Brep::Sphere()`, `Brep::TrimmedPlanarFace()`
  (+ `hole_loops_uv`) + `Mesh::ExtrudeCappedSolid()`/`Mesh::Cylinder()`/
  `Mesh::ConeToApex()`/`Mesh::Cone()`/`Mesh::RevolveProfile()`/
  `Mesh::LoftClosedRings()`/`Mesh::Torus()` were the only shapes/
  operations here; `Brep::Extrude()`/`Revolve()`/`Loft()`/`Sweep1()`/
  `Pipe()` (see above) now add the B-rep-level sweep class;
  `ExtrudeTapered()` (see above) closes draft angles for
  circle/arc/convex-polygon profiles, with 2-rail sweeps and
  variable-radius pipes still open.
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
  (see above), and a genuinely pathological many-reflex-vertex case has
  now been exercised too - a parametrically-built 6-tooth "comb" (12
  reflex vertices, tooth/gap widths only a few tessellation cells wide at
  32x32 divisions) clipped to its exact hand-derivable area (computed
  from the same parameters that built the vertex list, not eyeballed off
  coordinates), extruded, and proven watertight via a real Manifold
  union, same rigor every earlier concave-trim test here uses. `trim_polygon` must
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
- `SubD` wraps real Catmull-Clark refinement; exact limit-surface
  evaluation now exists at the VERTICES (`LimitPoints()`, see below -
  OpenNURBS turned out to ship a real per-vertex limit evaluator after
  all) and over regular faces (`ToNurbsPatches()`), but not at an
  arbitrary point inside an irregular face — `ToApproximateMesh()` is
  still the repeated-subdivision approximation there, not the true
  smooth surface. Interior creases are now
  supported (see below) but only that one crease option; no SubD editing
  (adding/removing faces, extrude, etc.), and no SubD ↔ Brep conversion
  (that direction is the stubbed `BrepForm()`/`GetSurfaceBrep()` this
  section already flagged).
- `.obj` support (`SaveObj()`/`LoadObj()`) round-trips geometry, writes
  real per-vertex normals, and now round-trips per-vertex texture
  coordinates too (see below) - but still no materials or groups, and
  `LoadObj()` still only reads `v`/`vt`/`f` lines (`vn` is read but
  discarded, since normals here are always geometry-derived). `.stl` now
  round-trips both ASCII and binary STL (see below). `.ply` (ASCII only -
  binary PLY is a disclosed, out-of-scope gap, see below) now round-trips
  geometry, normals, and texture coordinates too, and - unlike `.stl` -
  writes a genuine quad face as one native PLY face rather than splitting
  it into two triangles. `.obj`/`.stl`/`.ply` are still the only formats
  here - no glTF, FBX, etc.
- Adaptive/curvature-aware meshing: this gap now has two real layers.
  `NurbsSurface::CurvatureAt()`, `NurbsCurve::SuggestedSamples()`,
  `NurbsSurface::SuggestedDivisions()`, and the uniform-division
  `...Adaptive()` methods up through `Brep::TessellateAdaptive()`/
  `TessellateToClosedMeshAdaptive()` give a per-face curvature-informed
  *division count*. On top of that, `NurbsCurve::
  SuggestedParameterValues()`, `NurbsSurface::TessellateGridNonUniform()`
  /`SuggestedParameterValues()`/`TessellateGridNonUniformAdaptive()` (see
  above) now add genuine per-*region* adaptivity *within* a single
  face's own tensor grid: each direction gets non-uniformly-spaced
  breakpoints, denser only where that direction actually bends more, not
  one resolution smeared across the whole face - the earlier
  cylinder-wall example needed 32 uniform breakpoints in its curved
  direction but only 2 in its flat one, rather than 32 in both. What's
  still missing beyond that: true 2D per-*cell* refinement (a quadtree
  -style mesh where density can vary independently in both directions at
  once within one region, rather than a full row/column of the tensor
  grid all sharing the same breakpoint), which would need real
  T-junction handling this kernel doesn't have. The viewport/display
  engine, GPU path tracer, command engine, UI shell, visual scripting,
  undo system, installer, and everything else in the blueprint's roadmap
  remain entirely unstarted.

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

This section is stale as of chunk 2 - "no general solid construction" and
"no SubD support" were both true at the very start of this project but
aren't anymore (see the narrative and "What's still not done" above for
what's real today). Kept only for its one still-accurate point:

- A tolerance-management policy now exists (`include/dino8/kernel/
  tolerance.h` - see its own entry above): the ad-hoc constants this
  point used to list (`Mesh::MergeAndWeld`'s default tolerance,
  `LoftClosedRings()`'s/`IsRingPlanar()`'s relative-tolerance planarity
  check, `TessellateGridClippedExact()`'s grid-line nudge fraction) all
  route through named policy values now. What is still open is the
  TUNING those values will need once real modeling tolerances are
  decided (every value is still the literal it replaced), and routing
  the remaining brep.cpp/boolean.cpp literals the entry above lists.
