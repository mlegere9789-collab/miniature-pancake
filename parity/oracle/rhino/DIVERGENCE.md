# Dino 8 vs Rhino 8 divergence report

Dino 8 build `dino8-3d457f7e` (kernel runner `dino8-kernel/tests/extra/oracle_rhino.cpp`, run by CI through kvlite), Rhino golden build `8.36.26296.17001`, 200 cases, document tolerance 0.001. Field comparison and tolerance classes are compare.py's own, unchanged.

**73 match, 126 diverge, 1 excluded (golden marked provisional/hazard or declared unusable by the spec), 0 missing** (compare.py alone, without the exclusions, counts everything not matching as diverging).

Regenerate: `python3 -I parity/oracle/rhino/divergence_report.py` (reads `dino_results.jsonl`, the golden and `cases.jsonl`).

## Classes, ranked by rows blocked

| rank | class | rows |
|---|---|---|
| 1 | Both succeed, a metric (area, volume, length, count, points, degree, control points) is outside tolerance | 66 |
| 2 | Both succeed, metrics and bbox agree, only face/edge/vertex/Euler counts differ | 27 |
| 3 | Dino 8 returns a result where Rhino refused | 16 |
| 4 | Both succeed, only the bounding box differs | 9 |
| 5 | Dino 8 refuses or throws where Rhino succeeded | 8 |

A row is placed in exactly one class (the first that applies: outcome differs, then metric, then flag, then bbox, then counts).

Field incidence over the rows where both succeeded (a row counts once per differing field): vertices 83, edges 77, volume 41, area 32, bbox 30, faces 26, euler 17, solid 9, length 7, count 5, ctrlpts 3, points 2.

Bounding box: the kernel's tight box differs from the golden in 24 rows; for 17 of them the bounding box of the closed tessellation (diagnostic field `bbox_mesh` in dino_results.jsonl, within the 0.001 window) agrees with the golden, so the difference there is the kernel's tight box ignoring face trims, not a different shape: BOOL-BXBX-0005, BOOL-BXSP-0001..0004, BOOL-BXSP-0006..0009, BOOL-BXSP-0013..0014, BOOL-BXSP-0016, BOOL-BXSP-0022..0023, BOOL-CYCY-0010, BOOL-CYCY-0013, BOOL-CYCY-0015. The other 7 differ either way.

## 66 rows: Both succeed, a metric (area, volume, length, count, points, degree, control points) is outside tolerance

| operation | rows | differing fields (rows) | worst relative error in area/volume/length (volume sign flips excluded) | volume differs only by sign (rows) | cases |
|---|---|---|---|---|---|
| EXTR-POLY | 16 | edges 16, vertices 16, volume 16 | 0.00e+00 | 16 | EXTR-POLY-0001..0016 |
| REVO-RECT | 9 | area 9, edges 9, euler 9, faces 9, solid 9, vertices 9, volume 9 | 2.50e-01 | 0 | REVO-RECT-0001..0009 |
| CURV-INTP | 6 | bbox 6, length 6, ctrlpts 3 | 2.53e-02 | 0 | CURV-INTP-0001..0006 |
| BOOL-BXSP difference | 4 | bbox 4, edges 4, vertices 4, area 3, volume 2 | 7.17e-04 | 0 | BOOL-BXSP-0012..0015 |
| BOOL-BXSP intersection | 4 | area 4, bbox 4, edges 4, vertices 4, volume 3 | 3.29e-01 | 0 | BOOL-BXSP-0021, BOOL-BXSP-0023..0025 |
| BOOL-CYCY union | 4 | area 4, edges 4, vertices 4, faces 1 | 4.69e-03 | 0 | BOOL-CYCY-0001..0003, BOOL-CYCY-0005 |
| LOFT-CIRC | 4 | volume 4 | 0.00e+00 | 4 | LOFT-CIRC-0001..0004 |
| BLND-BOX fillet all edges | 3 | area 3, volume 3 | 7.78e-02 | 0 | BLND-BOX-0002..0004 |
| BOOL-SPSP union | 3 | area 3, edges 3, vertices 3, volume 2, euler 2, bbox 1, faces 1 | 5.00e-01 | 0 | BOOL-SPSP-0001..0003 |
| INTS-PLSP | 3 | count 2, length 1 | 1.30e-01 | 0 | INTS-PLSP-0002, INTS-PLSP-0005..0006 |
| BOOL-BXSP union | 2 | area 2, bbox 2, edges 2, vertices 2, volume 1 | 7.75e-04 | 0 | BOOL-BXSP-0006..0007 |
| BOOL-CYCY difference | 2 | area 2, edges 2, vertices 2, bbox 1 | 5.69e-03 | 0 | BOOL-CYCY-0008, BOOL-CYCY-0010 |
| BOOL-CYCY intersection | 2 | area 2, bbox 2, edges 2, vertices 2 | 3.79e-03 | 0 | BOOL-CYCY-0013, BOOL-CYCY-0015 |
| INTS-CRCR | 2 | count 2, points 2 | 0.00e+00 | 0 | INTS-CRCR-0001, INTS-CRCR-0005 |
| BOOL-SPSP difference | 1 | bbox 1, edges 1, vertices 1, volume 1 | 1.35e-03 | 0 | BOOL-SPSP-0006 |
| INTS-PLBX | 1 | count 1 | 0.00e+00 | 0 | INTS-PLBX-0006 |

## 27 rows: Both succeed, metrics and bbox agree, only face/edge/vertex/Euler counts differ

| operation | rows | differing fields (rows) | worst relative error in area/volume/length (volume sign flips excluded) | volume differs only by sign (rows) | cases |
|---|---|---|---|---|---|
| OFFS-SOL cylinder | 6 | edges 6, faces 6, vertices 6 | 0.00e+00 | 0 | OFFS-SOL-0013..0018 |
| OFFS-SOL sphere | 6 | euler 6, vertices 6 | 0.00e+00 | 0 | OFFS-SOL-0001..0006 |
| BOOL-BXBX difference | 4 | edges 4, faces 4, vertices 4 | 0.00e+00 | 0 | BOOL-BXBX-0008..0011 |
| BOOL-BXBX union | 4 | edges 4, faces 4, vertices 4 | 0.00e+00 | 0 | BOOL-BXBX-0002..0004, BOOL-BXBX-0006 |
| OFFS-SOL box | 4 | edges 4, vertices 4 | 0.00e+00 | 0 | OFFS-SOL-0009..0012 |
| BOOL-SPSP union | 2 | edges 2, vertices 2 | 0.00e+00 | 0 | BOOL-SPSP-0004..0005 |
| BOOL-SPSP difference | 1 | edges 1, faces 1, vertices 1 | 0.00e+00 | 0 | BOOL-SPSP-0007 |

## 16 rows: Dino 8 returns a result where Rhino refused

| operands (from the case arguments) | operation | rows | cases |
|---|---|---|---|
| disjoint operands | BOOL-SPSP difference | 2 | BOOL-SPSP-0009..0010 |
| tool strictly inside | BOOL-CYCY difference | 2 | BOOL-CYCY-0006..0007 |
| tool strictly inside | BOOL-CYCY intersection | 2 | BOOL-CYCY-0011..0012 |
| disjoint operands | BOOL-BXBX difference | 1 | BOOL-BXBX-0012 |
| disjoint operands | BOOL-BXSP difference | 1 | BOOL-BXSP-0018 |
| identical operands | BOOL-BXBX intersection | 1 | BOOL-BXBX-0013 |
| identical operands | BOOL-BXBX union | 1 | BOOL-BXBX-0001 |
| operands touching | BOOL-BXSP difference | 1 | BOOL-BXSP-0017 |
| operands touching | BOOL-SPSP difference | 1 | BOOL-SPSP-0008 |
| tool inside, sharing part of the boundary | BOOL-BXSP difference | 1 | BOOL-BXSP-0011 |
| tool inside, sharing part of the boundary | BOOL-BXSP intersection | 1 | BOOL-BXSP-0020 |
| tool strictly inside | BOOL-BXSP difference | 1 | BOOL-BXSP-0010 |
| tool strictly inside | BOOL-BXSP intersection | 1 | BOOL-BXSP-0019 |

Rhino's refusal class is `RuntimeError` throughout; Dino 8 returned geometry (see `route` in dino_results.jsonl). Counts and metrics of the geometry Dino 8 returned are not compared (the golden has none).

## 9 rows: Both succeed, only the bounding box differs

| operation | rows | differing fields (rows) | worst relative error in area/volume/length (volume sign flips excluded) | volume differs only by sign (rows) | cases |
|---|---|---|---|---|---|
| BOOL-BXSP union | 6 | bbox 6, edges 6, vertices 6 | 0.00e+00 | 0 | BOOL-BXSP-0001..0004, BOOL-BXSP-0008..0009 |
| BOOL-BXBX union | 1 | bbox 1, edges 1, vertices 1 | 0.00e+00 | 0 | BOOL-BXBX-0005 |
| BOOL-BXSP difference | 1 | bbox 1, edges 1, vertices 1 | 0.00e+00 | 0 | BOOL-BXSP-0016 |
| BOOL-BXSP intersection | 1 | bbox 1, edges 1, vertices 1 | 0.00e+00 | 0 | BOOL-BXSP-0022 |

## 8 rows: Dino 8 refuses or throws where Rhino succeeded

| operation | Dino 8 refusal | rows | cases |
|---|---|---|---|
| BLND-BOX chamfer all edges | `std::invalid_argument` | 4 | BLND-BOX-0007..0010 |
| BOOL-BXSP union | `BooleanNotClosed` | 1 | BOOL-BXSP-0005 |
| BOOL-CYCY difference | `BooleanNotClosed` | 1 | BOOL-CYCY-0009 |
| BOOL-CYCY intersection | `dino8::kernel::BooleanReassemblyError` | 1 | BOOL-CYCY-0014 |
| BOOL-CYCY union | `BooleanNotClosed` | 1 | BOOL-CYCY-0004 |

* BLND-BOX chamfer all edges: Dino 8 says: "dino8::kernel::ChamferConvexEdge: faces meet the edge at an endpoint but none has the simple trihedral corner pattern (its two loop neighbours on face"
* BOOL-BXSP union: Dino 8 says: "the general engine's result could not be sewn into a closed solid"
* BOOL-CYCY union: Dino 8 says: "the general engine's result could not be sewn into a closed solid"
* BOOL-CYCY difference: Dino 8 says: "the general engine's result could not be sewn into a closed solid"
* BOOL-CYCY intersection: Dino 8 says: "see boolean_general.h's own disclosed"

## Listed separately, not counted as defects

| case | why | outcome |
|---|---|---|
| BLND-BOX-0011 | spec-hazard: spec: degenerate golden result (flagged valid, not solid, bbox reaching 22.2); do not match | dino differs |

Fields the spec declares non-comparable and this report leaves out of the comparison: OFFS-SOL-0001 (bbox); OFFS-SOL-0002 (bbox); OFFS-SOL-0003 (bbox); OFFS-SOL-0004 (bbox); OFFS-SOL-0005 (bbox); OFFS-SOL-0006 (bbox).

## Golden values the Observer should re-check (still counted above)

* Negative volume reported for a valid closed solid (orientation convention of the reference, not a geometric volume): EXTR-POLY-0001..0016, LOFT-CIRC-0001..0004. Dino 8 reports the positive volume; the comparer treats the sign as a mismatch.

* BOOL-SPSP-0003 (union of two unit spheres whose surfaces touch at one point): the golden reports one sphere (volume 4.1887902048, 1 face); the union of the two is two spheres, volume twice that. Dino 8 returns the two.

## Reading the classes (what each divergence is, from the numbers in the two result files)

Nothing below was tuned: every statement is read off dino_results.jsonl and the golden, and each closed form is a standard formula.

1. **Both succeed, a metric is outside tolerance.**
   * EXTR-POLY (16) and LOFT-CIRC (4): the golden volume is NEGATIVE for a valid closed solid (the reference's orientation convention); Dino 8 returns
     the positive closed-form volume (triangle R=1, h=1: 1.29904; the frustum cases agree with pi*h*(r0^2+r0*r1+r1^2)/3). The 4 loft rows differ in
     nothing else. EXTR-POLY also differs in topology counts: the reference splits each cap boundary at the polygon corners (V=2n, E=2n+1, F=3), Dino 8
     keeps each cap boundary as one closed edge (V=4, E=5, F=3). The sign is an Observer query (below), the counts are a representation difference.
   * REVO-RECT-0001..0009 (revolve of a rectangle through 90, 180, 270 degrees, cap=true): Dino 8 returns the capped solid (3 faces, solid, V-E+F=2); the
     reference returns the single uncapped revolution surface (1 face, not solid), so area and volume differ by the two end caps. The 360 degree rows
     (REVO-RECT-0010..0012) match.
   * Booleans with a curved operand (BOOL-BXSP, BOOL-CYCY, BOOL-SPSP): area/volume off by 2e-4 .. 6e-3 on ordinary rows, 1e-2 .. 3e-1 on the thin
     slivers (BOOL-BXSP-0024, -0025, volume 0.00047 at -0025). The same rows carry far more edges than the reference (96 against 16 at BOOL-BXSP-0012, 204 against 9 at BOOL-CYCY-0005): the general
     engine bounds the trimmed faces with dense polylines of the intersection curve, which is also where the area error comes from.
   * BLND-BOX-0002..0004 (fillet of all 12 edges, r = 0.25, 0.5, 0.99): Dino 8 equals the exact rolling-ball closed form with spherical corners
     (area 6(a-2r)^2 + 12(a-2r)*pi*r/2 + 4*pi*r^2 = 21.35398, 18.56637, 12.69192 and Steiner's volume 7.69902, 6.87979, 4.25151 for a=2) to 10
     digits; the reference is 0.2%, 0.8%, 4.5% smaller in area, so its corner patches are not spherical. Same rows match at r=0.05.
   * CURV-INTP (6): the global interpolant of Dino 8 has one control point per data point (4, 6, 9 for degree 2 and 3 alike); the reference's degree 3
     curve has n+2 (6, 8, 11) and a different shape: length 0.1% .. 2.5% off, bounding box off by up to 0.2. Degree 2 control point counts agree.
   * INTS-CRCR-0001 (two identical circles) 282 hits against 1 point, INTS-CRCR-0005 (circles tangent externally) 7 hits against 1: IntersectCurves does
     not merge the cluster of hits inside the tolerance band. INTS-PLSP-0002, -0006 (plane tangent to the sphere): the exact section returns no contour
     where the reference returns the tangent point (the analytic IntersectPlaneSphere does report it: `alt` in the record). INTS-PLSP-0005 (plane
     0.001 below the pole, true circle length 0.28092): the exact section returns 0.24439. INTS-PLBX-0006 (plane on the top face of the box): 1 contour
     against the reference's 2.

2. **Both succeed, only topology counts differ (27).** Area, volume and bounding box agree.
   * BOOL-BXBX union/difference (8): Dino 8 leaves the coplanar faces of the planar engine's result split (26 faces where the reference has 12, 14 where
     it has 8, 10 where it has 6); no coplanar-face merge. BOOL-SPSP-0004, -0005, -0007: the counts differ through the polyline edges (48 edges against 2).
   * OFFS-SOL cylinder (6): Dino 8 gives 3 faces / 3 edges / 2 vertices (the cylinder with its radius and caps moved); the reference gives 4 faces and 6 or 10 edges.
   * OFFS-SOL box with positive distance (4): the reference result has 36 edges and 32 vertices (the offset corners carry extra topology) with the same
     sharp-cornered shape (area and volume equal the mitred closed forms, e.g. 59.44 and 29.568 at 0.1); Dino 8 returns the plain mitred box (12 / 8).
   * OFFS-SOL sphere (6): V=2 against the reference's V=4 and Euler 2 against 4 (a degenerate-trim sphere in the reference); area and volume agree.

3. **Dino 8 returns a result where Rhino refused (16).** The Dino 8 results are geometrically valid: box minus a sphere strictly inside it is the box with
   an inner void (volume 64 - 14.137 = 49.863, 7 faces), the intersection with an enclosed tool is the tool (14.137), a difference with a disjoint or
   touching tool returns the operand unchanged, identical boxes give the box. The spec (rhino8-observed-behaviour.md) asks Dino 8 to reproduce the
   reference's refusal classes; whether to refuse is a parity decision, recorded here and not decided here.

4. **Both succeed, only the bounding box differs (9 rows here; more inside the other classes).** See the bounding box paragraph above: where the
   closed tessellation's box agrees, the kernel's `GetTightBoundingBox()` is the cause (it takes the whole supporting surface of a trimmed face, e.g.
   -0.2..8.2 for a box union whose faces are planar).

5. **Dino 8 refuses where Rhino succeeded (8, plus BLND-BOX-0011 listed separately).**
   * BLND-BOX-0007..0010 (chamfer of all 12 edges, d = 0.05 .. 0.99): `ChamferConvexEdges` declines because the edges share vertices (its documented
     scope); chamfering the edges one after another with `ChamferConvexEdge` fails on the 9th edge ("faces meet the edge at an endpoint but none
     has the simple trihedral corner pattern"). A separate probe of 30 edge orders (the natural one, its reverse, 28 random) at d = 0.05 and 0.5 found
     none that completes. The chamfer corner (three chamfers meeting) has no kernel entry for the batch case.
   * BOOL-BXSP-0005 (box united with a sphere centred on its face plane), BOOL-CYCY-0004 (union), -0009 (difference), -0014 (intersection) (coaxial
     cylinders whose radii differ by 0.001, the document tolerance): the general engine's result cannot be sewn into a closed solid
     (`BooleanNotClosed`) or the reassembly throws (`BooleanReassemblyError`). These are the near-coincident and tolerance-edge cases.

## How the Dino 8 results were produced

`dino8-kernel/tests/extra/oracle_rhino.cpp` (test `extra::OracleRhino`, run with `DINO8_TEST_FILTER=OracleRhino`) reads cases.jsonl, runs each case in a
forked child with a 60 s alarm and prints one `D8RES` record per case; `fetch_dino_results.py` collects them from the CI job log. Operations are the
kernel calls the app reaches: Booleans take the planar engine for planar-faced operands and otherwise the general engine with SealGeneralBooleanResult and the
app's sewing fallback (the app's mesh cross-check and mesh fallback are not replicated: a mesh has no faces or validity to measure); offsets of a whole
sphere or a plain cylinder are the same primitive with the radius moved (the app's TryExactOffsetSolid), a convex planar solid goes to
OffsetSolidConvexPlanar; all-edge fillet is FilletConvexEdges, all-edge chamfer is ChamferConvexEdges and then one ChamferConvexEdge per edge; sections are
SectionBrepByPlaneJoined; curve/curve is IntersectCurves; bounding boxes are the kernel's GetTightBoundingBox; document tolerance 0.001 wherever the
kernel takes one. An operation that returns no geometry is recorded as ok=false, error `EmptyResult`. `dino_results.jsonl` is the unedited CI output.

