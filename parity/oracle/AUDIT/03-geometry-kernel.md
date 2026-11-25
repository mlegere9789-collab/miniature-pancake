# AUDIT 03: Geometry kernel (dino8-kernel, dino8-core, kernel CI, kverify/* and w0-* branches)

Auditor: Observer, read-only pass, 2026-11-24. Plain analysis of OUR work. No vendor material used.
Checkout audited: branch `observer-2026-11-23` at `42dd6499` (call it HEAD). Nothing was built or run: every "test passes" below
is a statement about what the repository contains and what CI is wired to run, not a result I reproduced.

## 1. Scope covered / not covered

Covered: `dino8-kernel/` (40 sources, 103,950 lines in `src/`; 39 public headers; `tests/`; `cmake/`; `docs/ACIS_SAT_NOTES.md`;
`PARITY_MAP.md` 10,794 lines; `PARITY_MAP_CURRENT.md`; `README.md` 5,195 lines), `dino8-core/` (HEAD holds only
`KERNEL_THREADS.md` and `kernel_tsan_probe/`), `.github/workflows/kernel-verify.yml`, `.kverify_filters`, the kernel-relevant
parts of `.github/workflows/dino8-app.yml` and `dino8-app/CMakeLists.txt`, third-party licence notes, and all 490 branches named
`kverify/*` (210), `kvlite/*` (144, includes kvperf/kvpy/kvwin replays) and `w0-*` (136). Branch triage used `git cherry HEAD <branch>`
(patch-id equivalence), not ancestry, because the integration lineage was built by cherry-pick/merge of copies.

Not covered / could not check: (a) GitHub Actions results: no `gh`, no network use, so I cannot say which kverify/kvlite runs were
green; every "green" in PARITY_MAP is unverified by me. (b) Nothing compiled or executed: no test result, timing or sanitizer
output was reproduced. (c) `dino8-app` geometry layers (`src/geom/*`, `io/*`) only sampled where the kernel depends on them.
(d) The Register (10,660 rows) belongs to other audit files; I only counted statuses. (e) I did not read the 5,195-line README or
the 10,794-line PARITY_MAP in full; I sampled by grep and read the lean snapshot. (f) Branch triage for the 20+ non-kernel
w0-* register/parity branches is by tip message only.

Orientation finding that shapes everything below: **there is no single trunk for kernel work.**
`origin/main` is 89 commits, dated 2026-09-05, and contains no `dino8-kernel/` at all. The kernel lives on
`claude/pdf-audit-i2bvwm` (2,963 commits, tip 2026-11-04), and HEAD is that branch plus 717 commits (integration "cand1" merge,
cherry-picked agent work 2026-11-04..11-16, then the Observer's 5 commits). The newest kernel fixes (11-15/11-16) sit on `w0-*`
branches that are in neither (section 4).

## 2. Capability matrix

"Real" = a named test exists in `tests/test_basic.cpp` (1,625 `RunTest` registrations, all defined functions are registered,
none orphaned) or `tests/extra/*.cpp` (578 `D8_EXTRA_TEST`), AND the CI that runs it exists. Important caveat for every row: the
only CI that runs the kernel suite is `kernel-verify.yml`, which fires only on pushes to `kverify/**` and `workflow_dispatch`
(finding F1). So "CI runs it" below means "a kverify push can run it", not "every change is gated".

| Capability | Real (named evidence) | Claimed without evidence | Fragile / wrong | Missing vs Rhino 8 / AutoCAD / Blender / SketchUp / Revit / Archicad |
|---|---|---|---|---|
| NURBS curves | `TestCurveDegreeElevation`, `TestCurveInsertKnotAt`, `TestCurveRemoveKnotAt`, `TestCurveSimplify`, `TestCurveLengthToTolerance`, `TestCurveFindSelfIntersections`, `extra::CurveReduceDegreeRecoversAPolynomialFromItsElevation`; thin wrapper over openNURBS evaluators (README states this) | "Curve ops 28/28 100%" (PARITY_MAP_CURRENT) is self-scored against closed forms, never against Rhino output | Most heavy lifting is delegated to openNURBS, so a test proves the wrapper not the algorithm; curve fit tolerance is a caller argument with no document tolerance | Rhino's document-tolerance model; Rhino golden corpus has 10 CURV cases and none were run against the kernel |
| NURBS surfaces | `TestSurfaceMakePeriodicExact`, `extra::SurfaceElevateDegreeKeepsTheShape`, `extra::SurfaceReduceDegreeRecoversASplineSurface`, `extra::SurfaceClassifyRecoversHandBuiltAnalyticSurfaces`, `TestSurfaceInterpolate*`, `NurbsSurface::Join` tests (`TestNurbsSurfaceJoin*`) | "Surface ops 25/5 91.7%" | `TrimmedPlanarFace` and grid tessellator are whole-cell in/out unless the exact-clip path is used (README "What's still not done") | Network surface, `NetworkSrf`-class tool: no kernel function (grep: zero); extend-surface kernel function absent (app only) |
| Trimming / B-rep topology | `extra::BrepFaceSeamSphere`, `extra::BrepFaceClassifyTrimmedFaces`, `TestBrepNakedEdge*` (4), `extra::LoopSpikeRemovalMergesOutOfOrderPointsOfAStraightRun`, ON_Brep `IsValid`/`IsSolid` asserted in sealed boolean tests | Topology 13/14 present/partial (74.1%) | Edge tolerance cannot be stored: public openNURBS `ON_Brep::SetEdgeTolerance` is a stub, kernel writes 0.0 (`brep.cpp:4128-4140`); no tolerant-edge model as in Parasolid/Rhino. Vertex welding is a quantised hash grid with a neighbour-cell patch (`brep.cpp:3604-3640`) | Tolerant edges/vertices, shell/face healing at Rhino `Join` parity unmeasured |
| Booleans, mesh | `BooleanCombine` via Manifold 3.5.2 (Apache-2.0): `TestBooleanUnion`, `TestBooleanIntersection`, `TestBooleanDifference`, `TestBooleanSymmetricDifference` (closed-form volumes); `extra::TypedReassemblyRefusalCarriesTheNonManifoldEdge` | None significant | Mesh result only (no B-rep); Manifold requires closed manifold input and throws otherwise | Mesh boolean tolerance/solver options as in Blender exact/fast, Rhino mesh boolean |
| Booleans, B-rep (planar / mixed / general / sealed) | `BooleanCombinePlanar` (exact), `BooleanCombineMixed` (plane/cylinder/cone enumerated by type pair), `BooleanCombineGeneral` (SSX-driven), `...Sealed`; `extra::SealedBooleanCorpusParallelCylindersIntersection`, `extra::SealedSliversLensOracleAgrees`, `TestBooleanCombineGeneralFlatSidedBossCrashRegression`, `extra::FlushThroughHoleDifferenceSealsAtEverySeamPlacement`, `extra::BossRoundOnSphereMatchesClosedForm`, `extra::PocketRoundInSphereMatchesClosedForm` | "Intersections & projections 29/29 = 100%" and "Boolean 11/16/2 = 65.5%" | Independent stress corpus (branch `w0-corpus`, `parity/corpus/BASELINE.md`) measured **136/208 = 65.4%** (golden 81.0%, evil 59.3%) on cand2: 45 throws, 4 timeouts (hole plates), 5 wrong volumes, near-tangent 11/35 = 31%, curved 0/11, many-holes 0/5. Sealed exact path covers box and cylinder-wall operands only, volumes are quadrature to 0.1%-1% (PARITY_MAP_CURRENT). General engine: curved intersection edges are dense polylines, genus-0 faces, one crossing chain per face pair (`boolean_general.h:19-27`); `general_boolean_sweep` (76 cases) is a measurement harness, deliberately not in CTest, README last records `closedmesh=0` count 22 | Freeform NURBS-vs-NURBS solid booleans with exact trimmed results; Boolean on `ON_Brep` from 3dm with tolerance model; Rhino `BooleanSplit`/`Trim`; SketchUp "intersect faces" semantics; Blender mesh boolean modes |
| Fillet / chamfer / blend | 128 Fillet, 59 Chamfer, 49 Blend named tests; closed-form rolling-ball cylinder fillets (`FilletConvexEdge`, `...Concave...`, `...Law`, `...Tapered`, `FilletCapRim`, `FilletPipeTee`, `FilletSurfaces`); `extra::PairMitreBoxCorner`, `extra::ChamferPairLegsUnequalDistances`, `TestFilletConvexEdgeResultTessellatesToAClosedMesh`; app `dino8_test_g2_blend`, `dino8_test_variable_blend`, `dino8_test_blend_adaptive_gap` (CTest) | "Blending 7/18/0 = 64%" (honest) | `FilletConvexEdge` scope: straight edge between two PLANAR faces; curved adjacent faces, variable radius on freeform edges are "out-of-scope future work" (`fillet.h:150-165`); `fillet.cpp` is 11,253 lines of special cases | General rolling-ball fillet on freeform/trimmed faces and edge chains with setbacks (Rhino FilletEdge/BlendEdge), variable-radius on any edge pair, vertex blends beyond trihedral; Blender bevel modifier profile/segments |
| Offset / shell / thicken | 183 Offset, 67 Shell, 29 Thicken tests; `offset_unified_tests` (per PARITY_MAP: 602 checks, 3000 random points), `extra::OffsetRuledSolidSquareFrustum`, `extra::OffsetRevolvedProfileCylinderAndRoundTrip`, `extra::ShellRevolvedProfileCylinderCupAndVariableFloor`, `extra::ThickenCylinderWallClosedForm` | "Offsetting 8/24 = 62.5%" | Solid offset only for planar-faced solids or hand-listed analytic cases; message at `offset_solid.cpp:137` says the general case "is not implemented" | General offset/shell of trimmed freeform solids (Rhino `OffsetSrf` solid, `Shell`, Revit/Archicad thick walls), self-intersection removal |
| Loft / sweep / revolve / extrude / pipe | 145 Sweep, 47 Revolve, 69 Extrude, 31 Loft, 15 Pipe tests; `extra::Sweep1TwistStraightRailDefaultIsTheHelicoid`, `extra::Sweep2ClosedSquareRingsFlat`, `extra::LoftSectionTangentsTwoCirclesAreAnExactHermiteBarrel`, `extra::LoftRationalTangentCircleSections`, `continuity2_tests` (G1/G2 vs bicubic closed form; in CTest as `dino8_continuity2_tests`) | "Sweep 12/22/0 = 67.6%"; Sweep2 "exact on straight rails, station interpolant on curved ones" | Sweep1 oracles only on straight/arc/ring rails; `Sweep2` curved rails not exact; star-shaped caps limited | Birail with full rail/section sync controls, Loft Rebuild/Refit options at Rhino parity, SketchUp Follow Me on arbitrary faces |
| Intersections (CCX, CSX, SSX, projection) | 81 Intersect-named tests, `extra::ProjectCircleToPlaneSphereCylinder`, `extra::SplitProjectedCurveArcCutsQuarterDisc`, `extra::SurfaceClosestPointRingWallMatchesBruteForce`; SSX default tolerance 0.001 with 0.02 mesh seeds (`surface_intersect.h:45-46`) | "100% (29/29)" | Mesh-seeded marching: near-tangent crossings are the measured weak class (31% in corpus); fixes (`w0-tangentfix`, `w0-ssxfix`, `w0-holes`) are unmerged | SSX with guaranteed topology (all components, tangent, overlap) |
| Tessellation / meshing | 64 Tessellat tests, `extra::RegionAdaptiveSphereMatchesClosedForms`, `extra::TessellateAdaptiveSphereFaceCountIndependentOfCentre`, `extra::HoledFaceTessellationAreaIsIndependentOfTheDivisionCount`, `dino8_test_brep_mesher` | "Tessellation 20/5 = 90%" | Kernel tessellator is its own uniform/curvature-aware grid, not Rhino's mesher; general-boolean results need a special closed mesher (`TessellateGeneralBooleanClosedMesh`) | Rhino mesh density/aspect/angle control parity (Rhino golden corpus has no mesh-quality cases), Blender remesh/decimate in kernel (decimate only on branches `w0-rw2-mesh`, `w0-rw3-bkg`) |
| SubD | 169 SubD tests, `extra::SubDBooleanSubDOfCreasedBoxesIsExactAtAnyLevel`, `extra::SubDSubdivideDoublesCreaseEdgesAndKeepsCornersAndCreaseMidpoints`, `extra::SubDSubdivideMatchesTheDeRoseSemiSharpReferenceVertexForVertex`, `extra::SubDFromNurbsToleranceTorusPatchIsWithinToleranceByTheClosedFormDistance`; Catmull-Clark is openNURBS's `ON_SubD` | "SubD 17/6 = 87%" | Limit-surface evaluation inside irregular faces is an approximation; SubD file I/O serialised by a kernel mutex because openNURBS is not thread-safe (`KERNEL_THREADS.md`) | Blender multires/sculpt/modifier stack; Rhino SubD tool parity measured against Rhino output (none run yet) |
| Tolerance policy | `tolerance.h` (constexpr, 1e-6 distance, 1e-4 edge join, 1e-6 alignment); `extra::QcThickenWeldedUnitSheetAtTenMillionIsOutwardOriented`, `extra::QcOffsetSolidPlanarUnitCubeFarFromTheOrigin`, `extra::SimplifyStaysWithinItsToleranceOfTheCurveItStartedFrom`, 81 Tolerance-named tests | "tolerance policy closed" (README, `tolerance.h` header) | Constants are compile-time and model-size independent; only 81 call sites route through `tolerance::k*` against ~1,300 raw literals (501x `1e-9`, 263x `1e-6`, 197x `1e-12`) in `src/`; three coexisting scales (1e-6 weld, 1e-4 join, 1e-3 SSX) | Per-document absolute/angle tolerance (Rhino default 0.001 / 1 deg; Rhino golden corpus records 0.001), unit-aware tolerance, tolerant edges |
| I/O: 3dm | `ONX_Model` wrapper, `extra::ThreadSafe3dmIgesStepFileIoConcurrentMatchesSerial`; app `File3dm.cpp` (4,000 lines) | "Round trip works" | No real Rhino-authored `.3dm` fixture in the repo (`git ls-files` finds none); only self-written files | Reading Rhino 8 files with all object types (extrusions, SubD, instances, layers, user text) is unverified |
| I/O: STEP / IGES / ACIS / DXF / DWG | Kernel: curve/surface STEP+IGES writers/readers (`step_curve.cpp`, `iges.cpp`), ACIS SAT text (13 `acis_sat_tests::TestAcisSat*`), SAB read/write with "UNVERIFIED tag table" (`ACIS_SAT_NOTES.md`, 23 UNVERIFIED lines); App: B-rep STEP/IGES `FileIgesStep.cpp` (4,117 lines), fixtures such as `step_plane_face.stp`, hostile-input IGES fixtures | "File I/O 11/4/2" | All fixtures hand-made; no file produced by a third-party CAD; SAT never read from a real ACIS product; STEP AP214 assemblies/PMI/colours unverified; DWG via GPLv3 LibreDWG (section 5) | Parasolid, JT, Revit RVT/RFA, Archicad PLN, SketchUp SKP, Blender .blend, FBX, USD: none; IFC geometry only in app |
| Mass properties / queries | `TestBrepVolumeAndAreaMatchClosedForms`, `extra::AnalyticPrimitiveSealedVolumeMatchesClosedForms`, `extra::RevSurfaceSphereIntegrals`, bit-identical accelerated-vs-brute-force spatial query tests | "98.3%" | Volume of sealed solids is quadrature (relative tol knobs in `VolumeOfSealedSolid`, `brep.cpp:1414`), not exact | Hydrostatics-grade exactness claims; Rhino `Volume` agreement at document tolerance unmeasured |
| BIM / drafting geometry (Revit, Archicad) | `arch_calc.cpp` (336 lines): stair, ramp, roof pitch, zone area calculators; tests `extra::ArchStairRulesCheck` and siblings | Register rows linked to these tests on `w0-rowimpl-bim` | Calculators, not elements | No wall/slab/roof/curtain-wall/stair solids with join rules, no parametric families, no constraint solver in kernel (only `dino8-app/src/commands/cmd_constraints.cpp`) |
| Mesh modelling (Blender, SketchUp) | `mesh.cpp` 7,697 lines: weld, quad conversion, LSCM unwrap (`extra::LscmUnrollsACylinderStripExactly`), Delaunay, convex hull, deform, 3MF/glTF/OBJ/STL/PLY/LAS I/O with spec fixtures | "SubD/mesh rows" | glTF/3MF fixtures are self-generated (`gen_mesh_io_fixtures.py`) | No modifier stack, bevel/loop-cut/knife in kernel, no sculpt kernel, no push-pull/inference kernel (app only), no mesh decimate/remesh in HEAD kernel tests |

## 3. Findings ranked by effect on parity

| # | Sev | Finding | Evidence | Fix |
|---|---|---|---|---|
| F1 | High | The 2,203-test kernel suite is not gated by any CI on a trunk. It runs only on pushes to `kverify/**` or manual dispatch, on one platform (Ubuntu 22.04, Release). The app workflow forces kernel tests OFF and never calls `dino8_kernel_tests`. Windows/macOS/ARM kernel behaviour (MSVC FP, libm differences) is untested for geometry. | `kernel-verify.yml:3-7,9-12`; `dino8-app/CMakeLists.txt:50-54`; `dino8-app.yml` has no `dino8_kernel_tests` (grep); `DINO8_PLAYBOOK.md:41` requires a kernel test "passing in CI" | Add a `kernel-tests` job to `dino8-app.yml` (or a PR-triggered kernel workflow) running the 12-shard suite on the default branch and PRs; add Windows and macOS legs; make the suite a required check. |
| F2 | High | No trunk. `main` is stale (89 commits, no kernel); the integrated kernel is on `claude/pdf-audit-i2bvwm` plus an unnamed 717-commit integration that HEAD carries; the newest Boolean/SSX robustness fixes are stranded on `w0-*`. A reviewer, CI run, or fresh clone of `main` cannot see the kernel. | `git rev-list --count origin/main` = 89; `git diff --stat origin/main HEAD -- dino8-kernel` = 256 files all added; `git cherry` results, section 4 | Pick one integration branch (HEAD is the best base), merge `w0-tangentfix`, `w0-ssxfix`, `w0-holes`, `w0-loopfix`, `w0-trikernel`, `w0-tesstopo-spherebool`, `w0-perfbool`, `w0-perfsect`, `w0-misck-r2` onto it, run the full suite, fast-forward `main`. |
| F3 | High | Boolean robustness is the weakest high-weight area and the self-score overstates it. Independent stress corpus (214 cases, closed-form oracles, cross-checked by sampling) gave 65.4% pass on cand2, near-tangent 31%, curved 0/11, many-holes 0/5, 4 timeouts, 2 invalid results at far origin (1e9), 5 wrong volumes (e.g. `E-COIN-SPHERE-R1e-6`). Meanwhile Intersections is scored 100%. | `origin/w0-corpus:parity/corpus/BASELINE.md`, `baseline/baseline_cand2.jsonl`; `PARITY_MAP_CURRENT.md` table | Merge the corpus into HEAD, wire it as a non-gating CI report then a ratchet (pass count may not fall), and re-baseline after the stranded fixes. Re-score Intersections from the corpus, not from closed-form unit tests. |
| F4 | High | Self-assessment is not connected to the Register or to the baselines. The 79.3% kernel headline is scored on 17 Parasolid-style categories chosen internally; all 10,660 Register rows are `status: open` with `evidence: null`; the Rhino golden corpus (200 cases, Rhino 8.36) has no Dino 8 comparison run yet. | `parity/register/*/*.jsonl` (counted: 10,660 open, 0 with evidence); `PARITY_MAP_CURRENT.md:7`; `parity/oracle/rhino/golden/rhino8-8.36...jsonl` (BOOL 70, PRIM 38, INTS 20, OFFS 18, EXTR 16, BLND 12, REVO 12, CURV 10, LOFT 4) | Build a kernel adapter that reads `cases.jsonl` and emits the same JSON; compare with `compare.py` at the Rhino tolerance (0.001) and publish pass rates per family. Replace the headline with that number. |
| F5 | High | Tolerance model cannot reach Rhino/AutoCAD behaviour: constants are `constexpr` (1e-6 weld, 1e-4 join, SSX 1e-3), not per-document and not scale-aware; edge tolerance cannot be stored because the public openNURBS stub; only ~81 of ~1,400 tolerance-like literals route through `tolerance.h`. Rhino golden outputs use tolerance 0.001, so results that are "right" at 1e-6 may differ in topology (what joins, what is a sliver) from Rhino. | `tolerance.h:44,60,123`; `brep.cpp:4128-4140`; literal counts from `grep -oE` over `src/*.cpp`; `README.md` "Known gaps" (TUNING still open) | Introduce a `ModelTolerance{abs, angle, rel}` object carried by `Brep` or call options; migrate literals in `boolean*.cpp`, `brep.cpp`, `surface_intersect.cpp` in tranches with the corpus as the safety net; store tolerance on edges via direct `m_tolerance` writes where the stub blocks the API. |
| F6 | High | General-surface coverage is narrow behind a big feature count: fillet only exact for straight planar-face edges plus listed analytic cases; solid offset only planar-faced or listed analytic; sealed boolean exact only for box and cylinder walls; general engine produces dense-polyline edges and genus-0 faces only. Rhino/AutoCAD users hit the freeform case first. | `fillet.h:150-165`; `offset_solid.cpp:130-137`; `boolean_general.h:1-27`; `PARITY_MAP_CURRENT.md` (Boolean batch notes) | Prioritise trimmed-freeform SSX + trim-reconstruction (shared by boolean, offset, fillet) over adding more special-case constructors. Track "freeform fraction" as a metric in the corpus. |
| F7 | Med | CI evidence is weakly gating even where it runs: `set +e` around runs; `dino8_continuity2_tests` output is `tail -3 \|\| true`; `.kverify_filters` per-feature runs only print counts; `dino8_surface_fillet_extra` target does not exist at HEAD (`\|\| true` hides it); kernel suite results live only in Actions logs, no artefact in git (`ledger/evidence` is empty). | `kernel-verify.yml:49,54,55,87,88,96`; `ls ledger/evidence` | Make every step fail the shard on non-zero exit; upload a machine-readable summary (the `DINO8_TEST_JSONL` support already exists in `evidence_jsonl.h`) and commit/attach it per release; delete dead targets. |
| F8 | Med | Branch sprawl hides real work and doubles CI cost: 210 kverify + 144 kvlite throwaway snapshots, 53 kverify branches are whole-suite re-runs (`full*`, `cand*`), many tips read "carry the verify workflow". 136 `w0-*` slices mixed with register/ledger work. No index says which is canonical. | section 4 | Delete merged/duplicate snapshots (list below), keep one tag per verified candidate, record the verified SHA in `PARITY_MAP_CURRENT.md`. |
| F9 | Med | Docs drift: `README.md` still says "What's still not done (as of chunk 2)" and its first checklist item "Curve/surface edit operations ... left for a later chunk" is unchecked, while the code has 100k lines; `PARITY_MAP.md` is a 10,794-line log (playbook already calls it archival); `PARITY_MAP_CURRENT.md` is not "a table" as its own protocol says (the paragraphs on lines 5-14 each run to several hundred words). | `dino8-kernel/README.md:9-20,5027,5180`; `PARITY_MAP_CURRENT.md:5-14`; `DINO8_PLAYBOOK.md` rule 1 | Regenerate the README capability list from the test registry; make `PARITY_MAP_CURRENT.md` a generated table from named tests; strip prose to a log. |
| F10 | Med | Kernel is mostly openNURBS (BSD-like) + Manifold (Apache-2.0) plus in-house B-rep engines. The in-house engines were tuned test-by-test with debug env switches that change geometry (`DINO8_NO_SEAM_REPAIR`, `DINO8_BOOL_DEBUG*`, 32 `getenv` sites), special thresholds (`kInteriorTouchTolerance 1e-4`, `kSeamReuseTol 1e-5`, `kWeldTol 1e-6`), and a 12,000-line boolean_general with comments recording "tried and rejected" fixes. High regression risk. | `boolean_general.cpp:685,1100,1710,9142`; `boolean_general.h:1655-1700`; `grep getenv src` | Remove or compile-out geometry-changing env switches in release; keep a regression corpus (F3) as the only gate for threshold changes. |
| F11 | Med | Interop never tested against third-party-authored files: no `.3dm`, `.sat`, `.stp`, `.igs` produced by another program is in the repo; ACIS SAT/SAB writer/reader built from prose docs, SAB tag table flagged UNVERIFIED; the SAT note says the vendor spec could not be read. | `git ls-files` (fixtures all `dino8-app/tests/*`, hand-made); `docs/ACIS_SAT_NOTES.md` | The Observer's licensed copies can produce reference files through each program's public export (numbers/flags only per SCOPE.md); store only checksums+counts, not vendor files. |
| F12 | Med | Thread-safety: kernel is thread-safe for tessellation/mass/booleans, but SubD and file I/O are serialised, 11 of the 13 races found are in openNURBS and its libuuid (3 on engine paths, 8 in file I/O; two openNURBS defects are patched at configure time and file I/O is guarded by one kernel mutex); a "hard configure error" guard exists. The audit evidence (TSan probe) is not in CI (`kernel_tsan_probe` has no workflow). | `KERNEL_THREADS.md` header; `CMakeLists.txt` patch guard; no workflow references `kernel_tsan_probe` | Add the TSan probe as a nightly job; track upstream openNURBS fixes. |
| F13 | Low | Test shape: 110k-line `test_basic.cpp` single translation unit; some extra files have one `Check(` for several tests (`match_edge_periodic.cpp`, `qc_degenerate_inputs.cpp`, `render_row_node_math.cpp`, `tessellation_region_adaptive.cpp`) which may be helper-wrapped, not proven empty. Assertion count per test is unknown because `Check` is also called from helpers. | `tests/extra/*.cpp` grep count | Run the suite with `DINO8_TEST_JSONL` and fail any test with zero checks. |
| F14 | Low | `dino8-core` is almost empty at HEAD (thread notes + TSan probe). Architecture spikes (scene, oplog, graph, ffi-probe) with results exist only on `w0-spike-a/b/b-r2/c/c-r2` and `w0-r1`. | `ls dino8-core`; `git diff --stat` per branch | Merge spike results as `docs/` plus tagged branch, or archive explicitly. |
| F15 | Low | `kernel-verify.yml` apt-mirror workarounds and retry loops (about 20 lines duplicated 3 times) show infrastructure flakiness was fought in-workflow. | `kernel-verify.yml:26-40` | Extract into a composite action. |

## 4. Branch triage

Method: tip commit message, date, `git cherry HEAD <branch>` (+ = patch not in HEAD), and whether the `+` commits touch
`dino8-kernel/` or `dino8-core/`. Counts are exact for the w0 group; kverify/kvlite are summarised (naming-pattern groups).

### 4a. kverify/* (210 branches)

All were pushed 2026-11-04..11-15. Almost every tip commit is "kverify: carry the verify workflow": a throwaway snapshot of a work
branch plus the workflow file, pushed only to get CI. Only 3 are ancestors of HEAD (`stage1`, `full-212041`, `full14-000848`).

| Group (count) | Examples | Verdict |
|---|---|---|
| Whole-suite re-runs (53) | `full2`..`full39`, `cand1-185128`..`cand4-full-235305`, `base-000450`, `ts6-*`, `dim-*` | Duplicates; zero kernel diff vs HEAD (`kfiles=0`). Delete after recording the verified SHA; keep cand4 as the last whole-suite snapshot. |
| Boolean / sealed / sliver / hole-pattern / flush (50) | `a8-steinmetz-oracle*`, `stee-*`, `sliv-*`, `sliver*`, `flush1`, `hp1`..`hp9`, `hpa`..`hpe`, `vol*`, `typed1`, `kbatch1`..`kbatch9`, `kernelbatch1` | Iterations of the same fixes; later sibling supersedes earlier (`sliv-234819` over `sliv-213130`). Content that landed is in HEAD by patch-id; delete duplicates. |
| Blend / fillet / chamfer (15) | `blend-221520`..`blend-230742`, `fsurf-*`, `law*`, `mrm*`, `lawfillet*` | Four near-identical `blend-*` snapshots; keep latest only. |
| Offset / shell / thicken (20) | `ae7-offrefit*`, `ose-*`, `tshell*`, `thk*`, `thkonly*`, `cap-000551` | `ose-shell` and `tshell*` are temporary probes ("WIP", "temporary"); abandon. |
| Sweep / loft / continuity (31) | `sweepadapt2`..`6`, `g2*`, `af05-*`, `sot-*`, `spe-*`, `xal-*`, `surf-*` | Same pattern: successive snapshots; keep final per family. |
| Trim / section / QC (16) | `qc2-*`, `qc3-*`, `sec2-*`, `tri-*`, `tri2-*` | Merged by patch-id; delete. |
| I/O (14) | `stp-*`, `stp1`..`stp5`, `dxf-*`, `stl*` | `stp5-051138` (17 ahead, 19 kernel files) is the largest I/O snapshot; its patches are in HEAD per cherry for `w0-step-r2` (25 equivalent). |
| App / engine batches (6) | `appbatch1/2`, `engine2/3`, `lsp*` | App work; out of kernel scope. |
| Tessellation / SubD (5) | `tess*`, `subd-*` | Merged by patch-id. |

Caveat on method for kverify: of the 210, 117 touch `dino8-kernel/`/`dino8-core/`; 33 of those have no patch-id-unique kernel commit
against HEAD. The other 84 look "unmerged" by patch-id, but HEAD took squashed/re-applied copies of the 2026-11-04..11 work (spot
check: `kverify/blend-230742` shows 11 unique kernel commits, all WIP/diag steps, yet HEAD holds the same feature as squashed commit
`71424ae8` with `extra::FilletBossOnSlab` tests and the five `TestFilletRevolveRimConcaveConeCorners`/`TestSurfaceFilletCreasedWall...`
registrations). Treat pre-11-11 kverify branches as merged in content unless a named test is missing from HEAD; I only verified
the blend/fsurf pair that way. Several tips are explicitly unfinished ("WIP ... (untested)", "diag: ... (temporary)",
"topology_shell probe ... (temporary)", "ose-shell WIP"); those snapshots carry no value beyond history.

Recommendation: keep the three ancestors' tags (`stage1`, `full14`, `cand4-full-235305`) as named verification points, delete the other
207 kverify refs; the Actions logs are the only artefact, so first export the last green run per candidate.


### 4b. kvlite/* (144) and replay variants

Tips read "kvlite: sections [...] filters [...]" (CI selector commits), "kvperf: jobs [...]", "kvpy: replay of ...", "kvwin:
replay of the windows job ...". These are CI launchers for the matching `w0-*` branch (`kvlite/credit1-*` is the same as
`w0-credit1`, which equals HEAD). Verdict: ephemeral; delete all once the `w0-*` branch they verify is merged or abandoned. Note the
`kvwin/*` (6) and `kvpy/*` (3) branches show Windows and Python CI were being replayed manually, which supports F1.

### 4c. w0-* branches (136), kernel-relevant ones (unique = patches not in HEAD; uk = of those, touching kernel/core)

| Branch | Date | Unique / uk | Tip message (abridged) | Verdict |
|---|---|---|---|---|
| w0-tangentfix, -r2, -old | 11-16 | 7/7, 5/5, 8/8 | near-tangent Booleans and SSX; EarClip ear-verdict cache (100-hole plate in seconds); "wip: polish target 1e-7" on `-old` | NOT merged, newest SSX/Boolean fixes. Merge `-r2` (latest); `-tangentfix-old` is WIP, abandon. Duplicate lineage of three. |
| w0-ssxfix | 11-16 | 4/4 | near-miss seeds only on spheres, closed-form rim circles, seam-crossing chains | NOT merged; overlaps `w0-loopfix` (shares commit `e58d4e55`) and `w0-tangentfix`. Merge with them. |
| w0-holes | 11-15 | 8/8 | counterbore sealed result, SSX polish only plane x cylinder, BuildLoop chords | NOT merged; addresses corpus "many-holes 0/5" and timeouts. Merge. |
| w0-loopfix | 11-15 | 1/1 | seam-crossing chains, scale-aware tolerance with fine retry | NOT merged; contained in `w0-ssxfix`. |
| w0-trikernel | 11-16 | 2/2 | reconcile sealed-corpus changes with nested-flush, hole-pattern, torus-seam work | NOT merged; integration fix, merge last. |
| w0-tesstopo, -r2, -run1, -spherebool | 11-13 | 2/2, 1/1, 1/1, 2/2 | `CapNakedLoops` Coons-patch capping; welded sphere/torus into engine | 4 near-duplicates, last is superset; NOT merged. |
| w0-perfbool, w0-perfsect, w0-perfci | 11-14/15 | 8/1, 8/1, 8/1 | BVH ray casting in planar booleans; section skips generic SSX for planar edges; perf_compare noise rule | NOT merged; the 7 non-kernel commits are shared perf-CI tooling. |
| w0-misck, w0-misck-r2 | 11-14 | 2/2 each | `VolumeOfSealedSolid` planar cap with curved trim | `-r2` supersedes; NOT merged. |
| w0-qcr | 11-14 | 1/0 | Boolean auto tolerance follows smaller operand | Tip is kernel-labelled but the one unique patch touches no kernel path; the kernel change may already be in HEAD or in `w0-loopfix`; verify before deleting. |
| w0-engine, w0-engine-r2 | 11-11/14 | 3/2, 2/2 | constrained Delaunay with holes; MatchSrf flat plane curvature | Partly in HEAD already (`delaunay.cpp`); remainder unmerged; `-r2` supersedes. |
| w0-curveops, w0-geomrepr, w0-geomrepr-r2, w0-fileio | 11-13/14 | 2/1, 1/1, 1/1, 2/2 | `LengthToTolerance`, `Simplify` budget, Hydrostatics stations, OBJ fixture fix | Small leftovers; merge or drop; `-r2` supersedes `geomrepr`. |
| w0-rowwork-kg, w0-rw2-kg, w0-rw2-mesh, w0-rw3-bkg, w0-rw3-bsim, w0-rw3-mesha, w0-rw3-meshb, w0-rw3-bim3, w0-rw2-bim, w0-rowimpl-bim, w0-rowimpl-render | 11-15/16 | 1-4 kernel each | "link N rows to named kernel tests" (BlendCrv, MergeSrf, VariableOffsetSrf, decimate, bim3_calc, IFC writer) | Register-evidence branches: kernel tests unmerged; merge (they carry the test ids the Register points to). |
| w0-spike-a, -b, -b-r2, -c, -c-r2, w0-r1 | 11-14 | 6-10 each, all in `dino8-core/` | scene graph, oplog, incremental graph, FFI probe, ADR-boundary with measured numbers | Not kernel code; research spikes with results docs. Keep `-b-r2`, `-c-r2`, `w0-r1` (final of each), delete earlier. |
| w0-corpus | 11-14 | 5/0 (adds `parity/corpus/`) | 214-case Boolean corpus + cand2 baseline 65.4% | NOT merged; highest-value evidence in the kernel area. Merge now. |
| w0-onpatch, w0-kthreads, w0-threadfix, w0-sealfix, w0-fixsphside, w0-fixb2s*, w0-fixots, w0-joinfix, w0-sme, w0-tol, w0-tessfix, w0-split, w0-srfmatch, w0-patch, w0-appbatch3, w0-blend, w0-rvsh*, w0-offrem, w0-step*, w0-credit1, w0-rhino-oracle | 11-11..16 | 0/0 | various | Fully contained in HEAD by patch-id: MERGED in content. Delete the branch refs. |
| Register/ledger/oracle branches (about 60: `w0-regfix-sketchup`, `w0-review-*`, `w0-register2`, `w0-regnorm`, `w0-ledger*`, `w0-taskgen`, `w0-gh*`, `w0-revit*`, `w0-archicad*`, `w0-autocad*`, `w0-forma*`, `w0-sketchup`, `w0-rw3-*` tooling) | 11-13..24 | many | parity/ledger only | Not kernel; belong to other audit files. Many unmerged (e.g. `w0-regfix-sketchup` 21 unique). |

Other long-lived refs: `claude/pdf-audit-i2bvwm` (the real trunk, tip 2026-11-04, 2,963 commits; HEAD is 717 ahead),
`backup/*` (2, stale), `claude/*` (11 unrelated product experiments: mediasuite, growmate, wildkey, etc., outside this audit).

## 5. Licence and contamination notes

Owner's intent: free, open source, never sold for gain. Findings:

1. **Repo root `LICENSE` and `README.md` are OpenAI Whisper's** (`LICENSE`: "MIT License, Copyright (c) 2022 OpenAI"; root README
   starts "# Whisper"; first commit `e0bf5da1`, "Add files via upload"). Dino 8 has no licence file of its own. Anything published
   today is nominally under someone else's copyright line and an MIT grant, which also allows resale, the opposite of the owner's
   intent. Fix: replace with a Dino 8 licence chosen by the owner (see 5.4) and remove the Whisper README and model card.
2. **GPLv3 is linked into the default app build**: GNU LibreDWG 0.14.8594 via FetchContent (`dino8-app/CMakeLists.txt:168-190`) for
   DWG import/export. `dino8-app/THIRD_PARTY_LICENSES.md` itself says a distributed binary is therefore GPLv3. The kernel
   (`dino8-kernel/CMakeLists.txt`) does NOT link LibreDWG: it links openNURBS, zlib, Manifold only, so the kernel library can stay
   permissive; only the app binary is affected. Note GPLv3 does not stop anyone selling copies, so it does not enforce "never sold
   for gain" either; if that is a hard requirement, a non-commercial clause (e.g. a PolyForm Noncommercial or CC-style licence
   for original code) is the only tool, and it is incompatible with a GPL link. Owner decision needed.
3. **No Blender (GPL) code or derivative found in the kernel.** Grep for `blender`, `bmesh`, `GPL` in `src/`, `include/`, `tests/`
   finds only comments naming Blender as a viewer or as the behaviour of "Tris to Quads" (`mesh.h:2069`). No Blender-sourced
   file, header or constant was seen. Blender behaviour evidence and algorithm cards must stay in `parity/oracle/blender` per SCOPE.md.
4. **Third-party table** (`THIRD_PARTY_LICENSES.md`) lists OpenNURBS (MIT-like), Manifold (Apache-2.0), GLFW, ImGui, Lua, pybind11. The
   kernel also bundles, through openNURBS, zlib and a libuuid copy; app links `opennurbs_public_freetype` (FreeType: FTL or GPLv2).
   Missing: a NOTICE file shipping Manifold's Apache NOTICE text and openNURBS's licence text with binaries (the table says they are
   required; no such file is in the repo). openNURBS is patched at configure time (`cmake/patches/opennurbs-8.34.26223-thread-safety.patch`);
   the patch is a modification of McNeel BSD-like code and should carry the upstream notice.
5. **Copied vendor material:** none found in the kernel. Fixtures are generated (`gen_mesh_io_fixtures.py`) or hand-built; no vendor
   `.3dm/.sat/.stp/.dwg` in git. Comments reference vendor products by name (Parasolid, ACIS, Rhino) as behaviour benchmarks, 8
   mentions in `src/`; the Observer firewall asks for no vendor identifiers in implementer-facing text, so keep those in comments only
   and out of public identifiers. `docs/ACIS_SAT_NOTES.md` records that only prose docs (MIT-licensed ezdxf docs) and web-search
   snippets were used and that the Spatial format document was unreadable; that is a sound clean-room record, but the SAB tag
   table is guesswork and should be removed or verified by Phase B (Observer) rather than shipped as a reader.
6. **Rhino golden corpus** (`parity/oracle/rhino/golden`) contains numbers and flags only (volume, area, bbox, counts); fine.
7. Process hazard, not code: the cherry-pick integration means authorship/attribution lines (`Co-Authored-By`, `Claude-Session`)
   appear in commit bodies; keep them out of any release licence file.

## Top actions (ordered)

1. Merge `w0-corpus`, then `w0-tangentfix-r2`, `w0-ssxfix`, `w0-holes`, `w0-trikernel`, `w0-tesstopo-spherebool`, `w0-perfbool/perfsect`
   onto one integration branch; make that `main`.
2. Put the 12-shard kernel suite on that branch's PR and push CI, on Linux, Windows, macOS; make failures hard.
3. Replace the Whisper `LICENSE`/README; decide the licence and the LibreDWG question before any public release.
4. Build the kernel adapter for the Rhino golden corpus and replace the 79.3% self-score with measured per-family pass rates.
5. Make tolerance a model parameter, not a constant.
