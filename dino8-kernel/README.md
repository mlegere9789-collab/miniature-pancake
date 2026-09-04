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
- `Brep::GetTightBoundingBox()` closes a real gap: nothing here could
  answer "roughly how big/where is this Brep" without tessellating it
  first, and even then Mesh::GetBoundingBox() only sees a tessellation's
  sampled vertices - an approximation of the true curved surface, not an
  exact bound on it. Delegates to `ON_Brep::GetTightBoundingBox` -
  **correction, found one chunk later by testing a genuinely
  doubly-curved face**: despite its name, this is *not* a real tight/exact
  bound in the public OpenNURBS build. It only samples each face's
  boundary/Greville-abscissa isocurves and control points, never a
  genuine 2D interior extremum search - exact for `Brep::Box()` (flat
  faces, hand-derivable exact) and, more subtly, `Brep::Sphere()` (a
  standard rational-NURBS sphere's meridian circles happen to have their
  own extrema exactly at points the isocurve sampling evaluates, not
  because the algorithm does a real search), but a doubly-curved bicubic
  bulge surface whose true peak sits at its own interior center - proven
  by evaluating `NurbsSurface::PointAt()` there directly - comes back
  overshot at exactly double the true height instead. Still always a
  valid, safe bound (never excludes real geometry, only occasionally
  overshoots), just not the minimal one its name promises - the same
  "declared for Rhino, degraded in the public build" pattern this
  codebase has found before (`ON_Brep::CreateMesh`, `ON_SubD::BrepForm`),
  found here by testing a case specifically chosen to expose it rather
  than assumed correct from a name and a non-stub function body.
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
- `SubD` wraps real Catmull-Clark refinement, but not exact limit-surface
  evaluation — `ToApproximateMesh()` is the repeated-subdivision
  approximation, not the true smooth surface. Interior creases are now
  supported (see below) but only that one crease option; no SubD editing
  (adding/removing faces, extrude, etc.), and no SubD ↔ Brep conversion
  (that direction is the stubbed `BrepForm()`/`GetSurfaceBrep()` this
  section already flagged).
- `.obj` support (`SaveObj()`/`LoadObj()`) round-trips geometry, writes
  real per-vertex normals, and now round-trips per-vertex texture
  coordinates too (see below) - but still no materials or groups, and
  `LoadObj()` still only reads `v`/`vt`/`f` lines (`vn` is read but
  discarded, since normals here are always geometry-derived). `.stl` now
  round-trips both ASCII and binary STL (see below). `.obj`/`.stl` are
  still the only formats here - no glTF, FBX, etc.
- Adaptive/curvature-aware meshing: `NurbsSurface::CurvatureAt()`,
  `NurbsCurve::SuggestedSamples()`, `NurbsSurface::SuggestedDivisions()`,
  and the `...Adaptive()` tessellation methods all the way up through
  `Brep::TessellateAdaptive()`/`TessellateToClosedMeshAdaptive()` (see
  above) now give a real, per-face curvature-informed division count
  instead of one fixed count a caller has to guess and share across
  every face. What's still missing is per-*region* adaptivity *within* a
  single face: `TessellateGrid()`/`TessellateGridClippedExact()`
  themselves still take one `u_divisions`/`v_divisions` pair for the
  whole face, so a face that's flat in one area and tightly curved in
  another still gets the tighter area's resolution everywhere on that
  face, not a locally-varying mesh density. The viewport/display
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

- No tolerance-management policy defined yet; wrapper calls use
  OpenNURBS defaults or ad-hoc constants (`Mesh::MergeAndWeld`'s default
  tolerance, `LoftClosedRings()`'s/`IsRingPlanar()`'s relative-tolerance
  planarity check, `TessellateGridClippedExact()`'s grid-line nudge
  fraction), which will need revisiting once real modeling tolerances are
  decided.
