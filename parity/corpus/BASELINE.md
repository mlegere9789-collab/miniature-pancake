# Baseline: boolean success rate of the cand2 kernel on the corpus v0

Result file: `parity/corpus/baseline/baseline_cand2.jsonl` (one JSON line per case, 214 cases = 208 boolean + 6 measure-only controls).
Regenerate this report: `python3 -I parity/corpus/summarize.py parity/corpus/corpus.json parity/corpus/baseline/baseline_cand2.jsonl --failures`.

## Headline

**136 of 208 boolean cases pass every invariant: 65.4 %** (golden 47/58 = 81.0 %, evil 89/150 = 59.3 %).
The 72 non-passing cases are all reported below with their failure class; none is relabelled. A case is `ok` only when the operation returns without exception, crash or timeout and every invariant in its `expect` block holds (emptiness, ON_Brep validity, solid with outward orientation, closed-manifold tessellation, volume, area, centroid, Euler characteristic, shell count).

Failure categories (exact counts, 208 boolean cases):

| category | count | meaning |
|---|---|---|
| ok | 136 | all invariants hold |
| crash (signal) | 0 | process died on a signal |
| throw | 45 | the operation raised an exception: 23 result not closed after sewing, 11 non-manifold fragment loops (engine refuses), 6 sealed result not a valid ON_Brep, 5 faces not consistently oriented |
| timeout | 4 | more than 30 CPU seconds (E-HOLES 4x4, 5x5, 8x8, 10x10 plates) |
| invalid result | 2 | a result came back but ON_Brep is not valid and/or not a solid (E-FAR-1e9-SPH-BOX-FACE-U, E-FAR-1e9-BOXBOX-CORNER-U) |
| wrong volume | 5 | valid solid with volume outside tolerance (E-INSCRIBED-CYL-U, E-INSCRIBED-CYL-AB, E-COIN-SPHERE-R1e-6-U, E-COIN-SPHERE-R1e-6-BA, E-SLIVER-BAR-I) |
| wrong emptiness | 1 | E-TAN-SPH-SPH-OV1e-3-I returned empty, expected a solid |
| wrong topology | 2 | Euler characteristic / shell count only (E-TAN-SPH-SPH-OV1e-3-U, E-SLIVER-BAR-AB) |
| mesh/area/centroid only | 13 | exact B-rep invariants hold but the closed mesher or the area/centroid measurement fails |

Per operation (union 79, difference 68, intersection 59, difference+union 2 multi-step):

| operation | cases | ok | throw | timeout | invalid | wrong volume | wrong empty | wrong topology | mesh only | success |
|---|---|---|---|---|---|---|---|---|---|---|
| union | 79 | 54 | 18 | 0 | 2 | 2 | 0 | 1 | 2 | 68.4% |
| difference | 68 | 41 | 13 | 4 | 0 | 2 | 0 | 1 | 7 | 60.3% |
| intersection | 59 | 39 | 14 | 0 | 0 | 1 | 1 | 0 | 4 | 66.1% |
| difference+union | 2 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100.0% |

## Method and provenance

- Kernel under test: `cand2` kernel sources; the six objects that differ from the shared prebuilt set (surface, features, mesh, fillet, boolean_general, brep) were rebuilt from cand2 at -O1 and linked with the prebuilt rest. No cmake/ninja build was used. The operation is what the app calls: `BooleanCombineGeneralSealed` / planar engine selection (`engine: auto`), result checked by `corpus_runner --run`.
- Oracles are closed forms (no kernel code is used to produce an expectation) and were cross-checked by an independent implicit-solid sampling selfcheck (185 cases checked, 29 tiny-volume cases skipped).
- Each case runs in its own forked process with a 30 CPU-second limit (a wall-clock backstop of max(4 x limit, 120 s) also applies).
- The run was interrupted once by a container restart after 175 cases; those 175 result lines were kept unchanged and the remaining 39 case ids were run with the same runner binary (`--only`), then concatenated. No case was run twice or selected.
- Controls (6 measure-only cases, no boolean) pass 6/6 and are excluded from the boolean rate.

## Success rates (boolean cases; controls reported separately)

| set | cases | ok | fail | throw | timeout | success |
|---|---|---|---|---|---|---|
| all boolean | 208 | 136 | 23 | 45 | 4 | **65.4%** |
| golden | 58 | 47 | 5 | 6 | 0 | **81.0%** |
| evil | 150 | 89 | 18 | 39 | 4 | **59.3%** |
| controls (no boolean) | 6 | 6 | 0 | 0 | 0 | **100.0%** |

## Per operation (boolean cases)

| operation | cases | ok | crash | throw | invalid result | wrong volume | wrong emptiness | wrong topology | mesh/area/centroid only | timeout | success |
|---|---|---|---|---|---|---|---|---|---|---|---|
| difference | 68 | 41 | 0 | 13 | 0 | 2 | 0 | 1 | 7 | 4 | 60.3% |
| difference+union | 2 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100.0% |
| intersection | 59 | 39 | 0 | 14 | 0 | 1 | 1 | 0 | 4 | 0 | 66.1% |
| union | 79 | 54 | 0 | 18 | 2 | 2 | 0 | 1 | 2 | 0 | 68.4% |
| ALL | 208 | 136 | 0 | 45 | 2 | 5 | 1 | 2 | 13 | 4 | 65.4% |

Operation by set:

| operation | set | cases | ok | crash | throw | invalid result | wrong volume | wrong emptiness | wrong topology | mesh/area/centroid only | timeout |
|---|---|---|---|---|---|---|---|---|---|---|---|
| difference | evil | 44 | 21 | 0 | 11 | 0 | 2 | 0 | 1 | 5 | 4 |
| difference | golden | 24 | 20 | 0 | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| difference+union | evil | 2 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| intersection | evil | 42 | 26 | 0 | 13 | 0 | 1 | 1 | 0 | 1 | 0 |
| intersection | golden | 17 | 13 | 0 | 1 | 0 | 0 | 0 | 0 | 3 | 0 |
| union | evil | 62 | 40 | 0 | 15 | 2 | 2 | 0 | 1 | 2 | 0 |
| union | golden | 17 | 14 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 |

## Per category (boolean cases)

| category | set | cases | ok | success |
|---|---|---|---|---|
| apex | evil | 12 | 9 | 75.0% |
| coincident | evil | 33 | 30 | 90.9% |
| curved | evil | 11 | 0 | 0.0% |
| degenerate | evil | 11 | 11 | 100.0% |
| many-holes | evil | 5 | 0 | 0.0% |
| near-tangent | evil | 35 | 11 | 31.4% |
| nested | evil | 5 | 4 | 80.0% |
| planar | evil | 3 | 2 | 66.7% |
| pole | evil | 3 | 3 | 100.0% |
| scale-ratio | evil | 2 | 1 | 50.0% |
| seam | evil | 10 | 6 | 60.0% |
| singular | evil | 3 | 3 | 100.0% |
| sliver | evil | 10 | 4 | 40.0% |
| tangent | evil | 7 | 5 | 71.4% |
| curved | golden | 44 | 33 | 75.0% |
| planar | golden | 14 | 14 | 100.0% |

## Per family (case id stem, boolean cases)

| family | set | cases | ok |
|---|---|---|---|
| APEX-BOX | evil | 5 | 2 |
| APEX-CYL | evil | 4 | 4 |
| APEX-POLE | evil | 3 | 3 |
| BOX-CYL-THRU | evil | 5 | 0 |
| BOXBOX-CORNER | evil | 3 | 2 |
| COIN-BOSS | evil | 12 | 12 |
| COIN-COAXIAL | evil | 5 | 4 |
| COIN-CYL-BOX | evil | 5 | 5 |
| COIN-LUNION | evil | 6 | 6 |
| COIN-SPHERE | evil | 5 | 3 |
| CYL-CYL-PERP | evil | 2 | 0 |
| EDGE-CONTACT | evil | 5 | 5 |
| EDGE-ON-FACE | evil | 4 | 4 |
| HOLES-10x10 | evil | 1 | 0 |
| HOLES-4x4 | evil | 1 | 0 |
| HOLES-5x5 | evil | 1 | 0 |
| HOLES-8x8 | evil | 1 | 0 |
| HOLES-RING | evil | 1 | 0 |
| INSCRIBED-CYL | evil | 4 | 2 |
| INSCRIBED-SPH | evil | 3 | 3 |
| NEST-COAXIAL | evil | 1 | 1 |
| NEST-COUNTERBORE | evil | 1 | 0 |
| NEST-MATRYOSHKA-BOX | evil | 1 | 1 |
| NEST-MATRYOSHKA-SPH | evil | 1 | 1 |
| NEST-SLOT-CHAIN | evil | 1 | 1 |
| POLE-CYL | evil | 3 | 3 |
| RATIO-BOSS | evil | 1 | 1 |
| RATIO-HOLE | evil | 1 | 0 |
| SEAM-CYL-HALF | evil | 3 | 3 |
| SEAM-HOLE | evil | 3 | 3 |
| SEAM-SPH-HALF | evil | 2 | 0 |
| SEAM-TORUS-HALF | evil | 2 | 0 |
| SLIVER-BAR | evil | 2 | 0 |
| SLIVER-NEEDLE | evil | 2 | 0 |
| SLIVER-PLATE | evil | 2 | 0 |
| SLIVER-WEDGE | evil | 4 | 4 |
| SPH-BOX-FACE | evil | 4 | 0 |
| STEINMETZ | evil | 3 | 3 |
| TAN-CYL-BOX | evil | 9 | 4 |
| TAN-CYL-CYL | evil | 4 | 1 |
| TAN-SKEW | evil | 5 | 1 |
| TAN-SPH-BOX | evil | 3 | 2 |
| TAN-SPH-BOX-POLE | evil | 4 | 0 |
| TAN-SPH-INT | evil | 4 | 1 |
| TAN-SPH-SPH | evil | 4 | 2 |
| TAN-SPH-SPH-POLE | evil | 2 | 0 |
| VERTEX-CONTACT | evil | 2 | 2 |
| BOX-CONE-SLAB | golden | 3 | 1 |
| BOX-CYL-BLIND | golden | 4 | 4 |
| BOX-CYL-OBLIQUE | golden | 3 | 1 |
| BOX-CYL-THRU | golden | 4 | 4 |
| BOX-DISJOINT | golden | 3 | 3 |
| BOX-IN-BOX | golden | 4 | 4 |
| BOXBOX-CORNER | golden | 3 | 3 |
| BOXBOX-ROT45 | golden | 4 | 4 |
| CYL-CYL-PARALLEL | golden | 3 | 0 |
| CYL-CYL-PERP | golden | 4 | 4 |
| CYL-CYL-SKEW | golden | 2 | 2 |
| CYL-TPIPE | golden | 2 | 2 |
| PLATE-4HOLES | golden | 1 | 1 |
| SPH-BOX-EDGE | golden | 3 | 1 |
| SPH-BOX-FACE | golden | 3 | 3 |
| SPH-CYL-X | golden | 3 | 3 |
| SPH-IN-BOX | golden | 3 | 3 |
| SPH-SPH | golden | 3 | 1 |
| TORUS-BOX-HALF | golden | 3 | 3 |

## Failure classes (boolean cases that are not ok)

| class | cases |
|---|---|
| throw: result not closed after sewing | 23 |
| throw: non-manifold fragment loops (engine refuses) | 11 |
| fail: tess_closed | 11 |
| throw: sealed result is not a valid ON_Brep | 6 |
| throw: faces not consistently oriented | 5 |
| fail: volume | 5 |
| timeout | 4 |
| fail: euler | 2 |
| fail: area | 2 |
| fail: empty | 1 |
| fail: valid | 1 |
| fail: solid | 1 |

## Sensitivity

- strict (every invariant in the manifest): 136 / 208 = **65.4%**
- exact B-rep only (ignoring failures that only concern the closed-mesh checks: tess_closed, area, centroid): 149 / 208 = 71.6%
- ran to a result without exception, crash or timeout: 159 / 208 = 76.4%

## Precision ladder (volume only; cases that returned a measured volume, golden + evil, non-empty expectation)

| relative volume error <= | cases | share of 146 measured |
|---|---|---|
| 0.01 | 139 | 95.2% |
| 0.001 | 127 | 87.0% |
| 0.0001 | 113 | 77.4% |
| 1e-06 | 84 | 57.5% |
| 1e-09 | 60 | 41.1% |

## CPU time per boolean case (ms, user+system of the case process)

median 106, 90th percentile 831, max 30249, total 207.4 s

## Failing cases

| id | result | first failing invariant / reason |
|---|---|---|
| G-BOX-CYL-OBLIQUE-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the sewn result is not a valid ON_Brep: ON_Brep.m_T[272].m_type = ON_BrepTrim::seam but m_iso is  |
| G-BOX-CYL-OBLIQUE-AB | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the sewn result is not a valid ON_Brep: ON_Brep.m_T[183].m_type = ON_BrepTrim::seam but m_iso is  |
| G-CYL-CYL-PARALLEL-U | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| G-CYL-CYL-PARALLEL-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 37 unpaired e |
| G-CYL-CYL-PARALLEL-AB | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| G-SPH-BOX-EDGE-I | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable; centroid: not measurable (no closed mesh) |
| G-SPH-BOX-EDGE-AB | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable; centroid: not measurable (no closed mesh) |
| G-SPH-SPH-I | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable; centroid: not measurable (no closed mesh) |
| G-SPH-SPH-AB | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable; centroid: not measurable (no closed mesh) |
| G-BOX-CONE-SLAB-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the sewn result is not a valid ON_Brep: ON_Brep.m_T[178].m_type = ON_BrepTrim::seam but m_iso is  |
| G-BOX-CONE-SLAB-I | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable; centroid: not measurable (no closed mesh) |
| E-TAN-CYL-BOX-OV1e-3-U | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| E-TAN-CYL-BOX-OV1e-3-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: every edge of the engine's result is shared by exactly two faces but the faces are not consistent |
| E-TAN-CYL-BOX-OV1e-3-AB | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: every edge of the engine's result is shared by exactly two faces but the faces are not consistent |
| E-TAN-CYL-BOX-OV1e-6-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 8 unpaired ed |
| E-TAN-CYL-BOX-OV1e-6-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 8 unpaired ed |
| E-TAN-SPH-BOX-POLE-OV1e-3-U | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| E-TAN-SPH-BOX-POLE-OV1e-3-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 5 unpaired ed |
| E-TAN-SPH-BOX-POLE-OV1e-6-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 8 unpaired ed |
| E-TAN-SPH-BOX-POLE-OV1e-9-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 8 unpaired ed |
| E-TAN-SPH-BOX-OV1e-3-U | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| E-TAN-SPH-SPH-OV1e-3-U | fail | euler: Euler characteristic 4, expected 2; shells: 2 shells, expected 1 |
| E-TAN-SPH-SPH-OV1e-3-I | fail | empty: result is empty, expected a solid of volume 1.5705345274070974e-06 |
| E-TAN-SPH-SPH-POLE-OV1e-3-U | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable; centroid: not measurable (no closed mesh) |
| E-TAN-SPH-SPH-POLE-OV1e-3-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 4 unpaired ed |
| E-TAN-CYL-CYL-OV1e-3-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 48 unpaired e |
| E-TAN-CYL-CYL-OV1e-3-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 48 unpaired e |
| E-TAN-CYL-CYL-OV1e-6-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 48 unpaired e |
| E-TAN-SKEW-OV1e-3-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 4 unpaired ed |
| E-TAN-SKEW-OV1e-3-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 24 unpaired e |
| E-TAN-SKEW-OV1e-6-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 4 unpaired ed |
| E-TAN-SKEW-OV1e-6-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 24 unpaired e |
| E-TAN-SPH-INT-IN1e-6-AB | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| E-TAN-SPH-INT-OUT1e-3-AB | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| E-TAN-SPH-INT-OUT1e-3-BA | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 6 unpaired ed |
| E-INSCRIBED-CYL-U | fail | volume: 13.710867252877216 vs 14.283185307179586 (sealed, error 0.04006935721926736 relative, tolerance 0.02); euler: Euler characteristic -4, expected 2 |
| E-INSCRIBED-CYL-AB | fail | volume: 1.1445433671561283 vs 1.7168146928204138 (sealed, error 0.33333319434967557 relative, tolerance 0.02) |
| E-COIN-COAXIAL-R1e-6-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: every edge of the engine's result is shared by exactly two faces but the faces are not consistent |
| E-COIN-SPHERE-R1e-6-U | fail | volume: 8.377592975955963 vs 4.188802771169569 (sealed, error 0.9999970000060013 relative, tolerance 0.02); area: 25.071183452485872 vs 12.566395747112965 (mesh); euler:  |
| E-COIN-SPHERE-R1e-6-BA | fail | volume: 4.188802771169572 vs 1.2566383179368756e-05 (sealed, error 333333.00003643584 relative, tolerance 0.02); area: 12.53712446665878 vs 25.132766361472136 (mesh); eul |
| E-SCALE-TINY-BOX-CYL-THRU-AB | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 64 unpaired e |
| E-SCALE-TINY-SPH-BOX-FACE-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 8 unpaired ed |
| E-SCALE-TINY-CYL-CYL-PERP-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 24 unpaired e |
| E-SCALE-HUGE-BOX-CYL-THRU-AB | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable; centroid: not measurable (no closed mesh) |
| E-SCALE-HUGE-SPH-BOX-FACE-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 8 unpaired ed |
| E-SCALE-HUGE-CYL-CYL-PERP-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 24 unpaired e |
| E-FAR-1e6-BOX-CYL-THRU-AB | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable |
| E-FAR-1e6-SPH-BOX-FACE-U | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable |
| E-FAR-1e8-BOX-CYL-THRU-AB | fail | area: 0 vs 70.2831853071796 (mesh) |
| E-FAR-1e9-BOX-CYL-THRU-AB | fail | area: 0 vs 70.2831853071796 (mesh) |
| E-FAR-1e9-SPH-BOX-FACE-U | fail | valid: ON_Brep::IsValid() is false; solid: closed but inside-out or of unknown orientation (signed volume 0); volume: 0 vs 1016.7551608191455 (mesh, error 1 relative, tol |
| E-FAR-1e9-BOXBOX-CORNER-U | fail | solid: IsSolid() is false (not a closed solid); volume: 0 vs 15 (mesh, error 1 relative, tolerance 6.83720051104548e-06); area: 28.00000121376446 vs 42 (brep); euler: Eul |
| E-SLIVER-PLATE-AB | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the sewn result is not a valid ON_Brep: brep.m_L[11] loop is not valid.; loop.m_type = 0 (must be |
| E-SLIVER-PLATE-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the sewn result is not a valid ON_Brep: brep.m_L[7] loop is not valid.; loop.m_type = 0 (must be  |
| E-SLIVER-NEEDLE-AB | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: every edge of the engine's result is shared by exactly two faces but the faces are not consistent |
| E-SLIVER-NEEDLE-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the sewn result is not a valid ON_Brep: ON_Brep.m_T[10].m_type = ON_BrepTrim::seam but m_iso is n |
| E-SLIVER-BAR-AB | fail | euler: Euler characteristic 4, expected 0; shells: 2 shells, expected 1 |
| E-SLIVER-BAR-I | fail | volume: 5.333333333332765e-06 vs 7.999997333332711e-06 (sealed, error 0.3333331111110562 relative, tolerance 0.02); centroid: not measurable (no closed mesh) |
| E-HOLES-4x4-AB | timeout | no result within 30 CPU seconds |
| E-HOLES-5x5-AB | timeout | no result within 30 CPU seconds |
| E-HOLES-8x8-AB | timeout | no result within 30 CPU seconds |
| E-HOLES-10x10-AB | timeout | no result within 30 CPU seconds |
| E-HOLES-RING-AB | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| E-NEST-COUNTERBORE-AB | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| E-SEAM-SPH-HALF-I | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable; centroid: not measurable (no closed mesh) |
| E-SEAM-SPH-HALF-AB | fail | tess_closed: the closed mesher did not produce a closed manifold mesh; area: not measurable; centroid: not measurable (no closed mesh) |
| E-SEAM-TORUS-HALF-I | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| E-SEAM-TORUS-HALF-BA | throw | general engine: dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or more fragment loops (non-manifold result) - out of scope; see boolean_general.h's own dis |
| E-APEX-BOX-SUNK1e-3-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 8 unpaired ed |
| E-APEX-BOX-SUNK1e-3-I | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 8 unpaired ed |
| E-APEX-BOX-SUNK1e-6-U | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: the engine's result is not a closed solid after sewing at up to 1% of its diagonal: 8 unpaired ed |
| E-RATIO-HOLE-AB | throw | result is not a closed solid: seal refused (BooleanCombineGeneralSealed: every edge of the engine's result is shared by exactly two faces but the faces are not consistent |
