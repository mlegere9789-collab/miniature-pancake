# AUDIT 04: the application (dino8-app/)

Plain analysis of OUR work. No vendor-program output, no Phase D material. Checkout: branch `observer-2026-11-23`
(tip is the `w0-rhino-oracle` line, last app commit 266d93f3, 2026-11-16). Read-only static audit: no build, no test run,
no CI log access (no `gh`, no network). "Passing" below always means "wired to run in CI", never "observed green".

## 1. Scope covered / not covered

Covered (read or measured):
* dino8-app/ (1112 tracked files): `data/commands.json`, `data/hatchpatterns.pat`, `data/i18n`, `src/commands` (65 files, 1352
  distinct `Reg(e,"Name"...)` registrations), `src/render`, `src/flow`, `src/io`, `src/arch`, `src/elec`, `src/script`, `src/plugins`,
  `CMakeLists.txt` (50 `add_test`), `tests/` (361 `*_script.txt`, 41 `test_*.cpp`, 24 `.py`, 176 shell sections plus the 14,049-line
  `smoke.sh`), `docs/INTEROP_LIMITATIONS.md`, `THIRD_PARTY_LICENSES.md`, `TESTING.md`, `AUDIT.md`, `RHINO8_KILLER_AUDIT.md`,
  `.github/workflows/dino8-app.yml` (628 lines).
* Counts were produced by scripts over the tree (registration regex, first-token match of script lines against the catalogue,
  pattern-style assertion regex). They are heuristics; error bars are stated where they matter.
* The Register JSONL files (`parity/register/*`) only to see how rows point at app tests.

Not covered:
* No test was executed; no CI run was inspected; no screenshot or pixel was looked at. Whether any named test currently passes is UNKNOWN.
* `dino8-kernel/` (geometry kernel), `dino8-core`, `mediasuite`, `parity/` tooling: other audit files.
* 490 app-touching commits on 144 `origin/kvlite/*` and other branches that are not in this checkout (see F-05). Newer work
  (for example 626e3f59 AutoLISP sections, 2026-11-16) is on those and was NOT read.
* Touch web client, packaging scripts and signing: read headers/CI wiring only.
* Behavioural parity itself (no baseline program was consulted; that is the Observer corpora's job).

## 2. Area matrix

Rating key: REAL = named test wired into CTest or `smoke.sh` AND that runs in Linux+Windows CI (pass status not observed);
CLAIMED = stated in a doc/header/commit, no test or no CI path found; FRAGILE = exists, but the test or design is weak or
contradicted; MISSING = no implementation found.

| Area | Rating | Evidence (file) | One-line reason |
|---|---|---|---|
| Rhino command catalogue (1071 names) | REAL (existence) / CLAIMED (behaviour) | `data/commands.json`; `src/commands/cmd_*.cpp` | All 1071 are registered (1057 by literal `Reg`, 14 by loops). `docs/site/commands.json` says Implemented 1054, Partial 1, Planned 0. Status is self-declared; no gate ties "Implemented" to a test (219 of 1071 never appear as a script command, first-token heuristic). |
| Rhino test coverage by script | REAL (existence) | `tests/*_script.txt` | 852/1071 catalogue names appear in at least one test script or shell file; 751 in a `*_script.txt`. "Appears" is not "asserted". |
| Command parity vs Grasshopper, Blender, SketchUp, Revit, Archicad, Forma | MISSING | `src/commands` | No Blender/SketchUp/Forma command vocabulary at all (no Orbit, PushPull, FollowMe, Sculpt brushes, Modifiers, Armature, Animation, Geo-nodes, Wind/Sun analysis). Revit/Archicad: 17 flat arch/mech/MEP generators only (area BIM). |
| AutoCAD drafting (dimension, hatch, layout, blocks) | REAL for a subset, CLAIMED for names | `cmd_annotate*.cpp`, `cmd_draftdocs.cpp`, `cmd_drafting*.cpp`, `tests/sections/94_dd*.sh`, `dd_check.py` | DimLinear/Aligned/Angular/Radius/Diameter/Ordinate/Baseline/Continue/Space, Hatch with .pat library, Layout, Table, MultiLeader, GD&T, SheetSet, Standards check exist with numeric DXF checkers. AutoCAD command names absent: no Xref/XAttach, MView, Plot, DimStyle, MLeader, Pedit, Break, Lengthen, Attdef/Attedit, Wipeout, Overkill (name probe; aliases may cover a few). No AutoCAD alias table. |
| Hatch pattern library | FRAGILE (provenance) | `data/hatchpatterns.pat` (107 lines, ~35 patterns) | Pattern names and descriptions for ANSI31-38, ISO02W100-15W100, BRICK, HONEYCOMB, DOTS, CROSS, GRASS, GRAVEL, NET read like Autodesk's acad.pat text. See section 5. |
| Rendering: CPU path tracer | REAL (small) | `src/render/PathTracer.*`, `tests/test_bvh.cpp` (ctest `dino8_bvh`), `render_hdr_script`, `raytrace_script` | NEE+MIS, glass, Russian roulette, ACES, bilateral denoise, HDR/EXR out. Only the BVH is checked against ground truth (brute force). No furnace/Cornell/luminance test. |
| Rendering: real-time GL (shadow maps, SSAO) | REAL (qualitative) | `GlRenderer.cpp`, `ssao_script`, `shadow_script` | Per-light shadow atlas (3x3 PCF), SSAO, procedural sky. Tests compare pixels of two renders of the same app (darker near a corner, two shadows present). No reference image. |
| Rendering: GPU raytraced viewport | CLAIMED | `GpuRaytracer.*`, `tests/gpu_render_notes.md` | Fragment-shader tracer (GL 3.3 core, no compute). CI has no GPU (Mesa llvmpipe on Windows, xvfb on Linux, no GL window on macOS), so GPU trace output is not checked. Notes admit a silent-failure bug that `gl_error=0` hid. |
| Outscape (Enscape-equivalent) | MISSING | grep `outscape|enscape` in `src`: 0 hits | No product, name, asset library, time-of-day/sun study UI, walkthrough/animation/video, VR, live link, or real-time GI. Material model is Phong-like (diffuse/specular/gloss/reflectivity/transparency/emission; `Document.h:313`), no roughness/metallic/IOR/normal/bump/SSS/PBR graph. 48 presets + procedural textures exist. |
| Dino Flow node editor (Grasshopper-like) | REAL (small) | `src/flow/*`, `flow_*_script`, `94_fr*.sh`, 23 `.dflow` fixtures | Solver with dirty propagation, list matching, trees, undo, JSON persistence, panel. ~98 literal node definitions plus row nodes (Grasshopper has thousands). No `.gh`/`.ghx` read or write (0 hits). Row nodes have exact closed-form checks. |
| Plug-in system | REAL (mechanism) | `include/dino8_plugin.h`, `src/plugins/*`, `plugin-sdk`, `plugin-templates`, 17 marketplace scripts | Native `.dll/.so` ABI v2, SDK, templates, index, reviews. Ecosystem: 4 first-party sample plug-ins; "no review team yet" (`docs/COMMUNITY.md`). No compat layer for Rhino or Grasshopper plug-ins. |
| Marketplace trust | FRAGILE | `Marketplace.cpp:157,413`; `DigitalSignature.cpp` | Downloads and loads native code; `sha256` is optional per entry; no plug-in signature. Hand-rolled RSA/BigUint is used for "signing exported files" only. |
| File I/O: .3dm | REAL | `File3dm.cpp` (OpenNURBS), `*3dm_write_check.cpp`, `*_reopen_script` | Round-trip checks for hatch, text, leader, dims, blocks, groups, views; most compare reopened object properties. Reading in real Rhino was not part of any CI test. |
| File I/O: DXF | REAL | `dxf_*_script`, 19 `.dxf` fixtures, `dxf_check.py` | Many fixture-based tests with independent stdlib checkers. |
| File I/O: DWG (LibreDWG) | REAL (small) / FRAGILE (design) | `FileExchange.cpp`, `dwg_script`, `dwg_reopen_script`, `cmake/patch_libredwg.cmake` | Export is DXF text then LibreDWG DXF reader then DWG writer, so DWG fidelity equals DXF export fidelity. Header text says import skips TEXT/MTEXT/DIMENSION/HATCH/SPLINE; changelog says they are read: contradictory docs. GPLv3 (see F-06). |
| File I/O: IFC | CLAIMED (beyond mesh) | `FileIgesStep.h:44-70`, `ifc_script` | Writes valid-looking IFC4 with every object as `IFCBUILDINGELEMENTPROXY` + `IFCTRIANGULATEDFACESET`; reads only triangulated face sets. No walls/slabs/types, psets, storeys beyond one. Comment says IfcOpenShell validated it "during development"; CI only greps the file for `FILE_SCHEMA(('IFC4'))`. |
| File I/O: glTF/GLB | REAL | `File3dm.cpp:3797`, `gltf_io_script`, `sections/89_gltf.sh`, `gltf_oracle.py` | Independent parser checks accessors, min/max, alignment, normals; round-trip. Geometry only (no PBR materials beyond base colour/alpha). |
| File I/O: STEP/IGES/SAT | REAL (STEP/IGES) / FRAGILE (SAT) | `FileIgesStep.cpp`, `FileSat.cpp`, `rwi_sat_script` | `FileSat.h` says no real ACIS-written file was available; SAB layout "UNVERIFIED". `INTEROP_LIMITATIONS.md` still says ACIS is unsupported (contradiction). SAT test uses our own Python reader as oracle. |
| File I/O: FBX, SKP, RVT, PLN, .blend, USD, DAE, 3DS, DWF | MISSING | no code | Only OBJ/STL/3MF/PLY/XYZ/PTS/LAS/glTF/IFC/SAT/STEP/IGES/DXF/DWG/3dm/SVG/PDF/web viewer. |
| BIM (Revit/Archicad-like) | CLAIMED / thin | `src/arch/ArchComponents.*` (779 lines), `arch_script`, `cmd_arch.cpp` | Parameter records rebuilt into meshes (Wall, Door, Window, Slab, Roof, Stair, Column, Beam + bolts, I-beams, duct/pipe/conduit). No levels, families, types, parameters beyond record fields, room/zone model, views, sections, sheets, phasing, worksets, schedules beyond a text list. Test asserts message strings only. |
| SubD / mesh tools | REAL | `sdbool`, `sdsym`, `tosubd2..6`, `rwms_*`, `subd_script`, `sculpt_script` | Many families with counts. Some pins are own-output goldens (F-02). No Blender-style sculpt brush set or modifiers. |
| Python scripting | FRAGILE | `PythonEngine.*`, `CMakeLists.txt:146-150`, `python_script.txt` | `dino8` module (~93 bindings), not `rhinoscriptsyntax`/RhinoCommon/`bpy`. **OFF by default on Windows** (the shipped platform): `RunPythonScript` reports unavailable and smoke skips the Python checks there. |
| Lua `rs.*` scripting | CLAIMED | `LuaEngine.cpp:1385` | Defines a `rhinoscriptsyntax` global alias so pasted snippets "need fewer edits"; ~176 distinct capitalised names; fidelity to the real API not measured. |
| AutoLISP | REAL (subset) | `Lisp.cpp` (2262 lines), `lisp*_script`, `60_lsp*.sh`, RW3ACS sections on kvlite branch | Self-described SUBSET: 167 literal builtins plus a vla-/vlax- object model; negative controls and independently computed results. No Visual LISP reactors, no dialogs (DCL). |
| Performance claims | CLAIMED WITHOUT EVIDENCE | `tests/*_bench.cpp`, `RHINO8_KILLER_AUDIT.md:76-78` | Benches print old-vs-new ratios of our own code; `find_bench.cpp` has no threshold; no comparison to any baseline; `stress.sh`/50k-object claims not run in CI as an assertion (not found in workflow). |
| i18n | REAL (coverage) / CLAIMED (quality) | `data/i18n/*.json` (11 files x 258 keys), `test_i18n_coverage`, `test_i18n_cmddesc` | Key coverage and "differs from English" enforced; translation provenance (machine vs human) not recorded. Command descriptions translated for a "bounded subset" only. |
| Tutorials | REAL (pipeline) | `tutorials/`, `docs/tutorial_scripts`, `verify.py`, CI `TUTV_` | 10 scripted tutorials, silent stills at 10 fps; CI records and ffprobes them. No narration, nothing published. |
| Packaging / signing | REAL (Windows, Linux), CLAIMED (macOS notarization) | workflow lines 105-336, 399-500 | Inno Setup, cpack .deb/.tar.gz/.dmg, Authenticode self-test, verify-release negative tests. Signing steps inert without secrets. |
| CI platform coverage | FRAGILE | workflow | Linux: build + ctest + full smoke (90 min cap) + touch Playwright + tutorial videos. Windows x64: build + ctest + smoke under Mesa (60 min cap). macOS: build + ctest only (no GL window on runner, so no command/render coverage). Windows ARM: build only, `continue-on-error`. Push trigger only `main` and `claude/pdf-audit-i2bvwm` (`dino8-app.yml:5`); `main` last changed 2026-09-05; work branches get CI only via pull requests. |

## 3. Findings ranked by effect on parity

Severity: High = blocks a parity claim or creates legal/owner-intent risk; Med = large gap or misleading evidence; Low = hygiene.

| ID | Sev | Finding | Evidence | Fix |
|---|---|---|---|---|
| F-01 | High | Vendor help text is shipped in the product. 501 of 1071 catalogue entries carry a `help` body (708,663 characters) that names McNeel 337 times, "Rhinoceros" 339, "User's Guide" 65, and includes toolbar/menu paths. README says the app ships "the full reference help text". All 1071 one-line `description` strings come from the same source. The docs site and `test_i18n_cmddesc` build on it. | `data/commands.json`; `README.md:15-24`; `docs/gen_command_docs.py:6-8`; `docs/site/commands_data.js` (350 KB derived data) | Treat as a clean-room breach. Remove `help` and `description` from the catalogue, keep only command names, own-words one-liners and option names; regenerate docs site; drop the "full reference help" claim; scrub git history only if the owner decides so. |
| F-02 | High | Test evidence is mostly self-referential. Of ~3,975 pattern-style assertions (`check`/`*_has` lines in `smoke.sh` and `sections/`), 2,824 contain a digit but only 272 contain a decimal or exponent; most digits are object counts or message text ("Wall #1 added."). Several are pins of our own output ("760 face(s), 32 crease edge(s)", "CHANGED PIN: 330 -> 338 (printed by the CI run)"). No assertion compares to a number measured in a baseline program. The strongest tests (closed-form volumes, 1e-9 residuals with a negative control, independent Python readers) are good practice but prove internal consistency and math, not baseline behaviour. | `smoke.sh:3827-3840`; `tests/sections/94_rwdm_rotate.sh`; `fillet_script` checks; `gltf_oracle.py` | Add a golden-vector layer: Observer corpora (numbers+flags) become fixtures, tests compare with tolerances recorded per row. Mark pins as "regression pin" vs "oracle". Keep the negative-control pattern. |
| F-03 | High | Parity breadth is Rhino-only. Catalogue 1071 Rhino commands; 295 other registered names are Dino-specific (view aliases 32, arch/mech 28, drafting 22+13+12, flow/plug-in 21, macros 16, layer 13, ...). Nothing named for Blender, SketchUp, Forma; Revit/Archicad only as flat generators; AutoCAD only as Dino-named subsets. Register rows (10,660) have `status: "open"` in every JSONL; only ~2,300 rows hold a non-placeholder `test_id` (rhino 1368, archicad 924, grasshopper 773, forma 206, autocad 40, blender 9, revit 3 counted per acceptance entry, so duplicates possible). | `src/commands/cmd_*.cpp`; `parity/register/*/*.jsonl` | Build the Register-to-test index as a CI artefact: for each row, test id exists, test runs, test is oracle-backed. Until then report parity as "Rhino catalogue registered", not "x% of nine programs". |
| F-04 | High | Outscape does not exist in the app. Zero source hits; a render pipeline exists (CPU path tracer, GL shadow/SSAO, GPU fragment tracer) but with a legacy material model, no walkthrough/animation/VR, no asset library, no live GI, and no luminance, histogram, or timing test. | `grep -ri outscape\|enscape src` empty; `Document.h:313-331`; `PathTracer.h:1-9` | Define Outscape rows first (coordinator), then add measured-output tests (luminance histogram on fixed scenes, resolution, frame time) as the SCOPE file describes. |
| F-05 | High | Evidence is split across unmerged branches and the CI trigger does not cover them. `origin/main` is stale (last app change 2026-09-05; the work line has 3,625 commits it lacks); 490 app-touching commits across 144 `kvlite/*` branches are not in this checkout; push CI runs only on `main` and `claude/pdf-audit-i2bvwm`. Any "passing" claim for work on `w0-*`/`kvlite/*` depends on pull-request runs that this audit could not see. | `git rev-list --count --remotes ^HEAD -- dino8-app` = 490; `dino8-app.yml:3-8` | Integrate or retire branches; add `w0-*`, `observer-*`, `kvlite/*` to the push trigger or require PRs; publish a per-commit CI status table into `parity/`. |
| F-06 | High | Licence conflict with the owner's intent. A default build links LibreDWG (GPLv3, fetched by CMake) into every binary, so the distributed binary is GPLv3, which permits commercial sale by others; SCOPE.md states that conflict as the reason Blender code must not be copied. Root `LICENSE` is MIT with "Copyright (c) 2022 OpenAI" (template residue, wrong holder). | `CMakeLists.txt:168-206`; `THIRD_PARTY_LICENSES.md` (own admission); `LICENSE:1-3` | Owner decision: (a) accept GPLv3 for the binary and say so in LICENSE/README, (b) move DWG into an optional separately-distributed plug-in/process, or (c) write a DXF-only core. Fix the copyright holder to LegeLabs. |
| F-07 | Med | CI does not cover macOS or Windows ARM behaviour. macOS runs only the 41 ctest executables (no command, render or file tests); Windows ARM is build-only with `continue-on-error: true`; Windows runs smoke with Python OFF so Python and the HTTP `/run/python` checks skip. | workflow 339-343, 413-430; `CMakeLists.txt:146`; `smoke.sh:8374` | Run `--script` smoke on macOS with a software GL or a headless offscreen context; turn Python ON for the Windows installer or ship embeddable Python; drop `continue-on-error` once stable. |
| F-08 | Med | The test harness is one 14,049-line `smoke.sh` plus 176 sections, run as a single CI step; several places `exit 1` immediately when a script process exits non-zero (e.g. boolean, render), which hides all later sections; 112 `skip` branches; `cn2_script.txt` and `annotate2_shot.txt` are referenced by no shell file. Time to first signal is 15-90 minutes. | `smoke.sh:3829-3831`; `tests/README_sections.md` | Run sections as parallel CI jobs (a runner already exists: `run_sections.sh -j`), replace `exit 1` with `fail=1`, fail CI on any `skip` outside a documented allow list. |
| F-09 | Med | BIM is a parametric-mesh toolkit, not a BIM model. IFC export maps everything to proxy elements; IFC import reads only `IFCTRIANGULATEDFACESET`. The BIM test asserts message strings ("Wall #1 added.", "Architectural schedule (8 component(s))"), not geometry volumes or openings. (The kvlite `bimarch` branches add hand-derived stair/ramp/zone numbers: better, not merged here.) | `ArchComponents.h:1-45`; `FileIgesStep.h:44-70`; `smoke.sh:4938-4950` | Define a BIM data model (levels, types, host relations, properties) and IFC entity mapping; test with element counts, volumes and opening areas against hand calculations. |
| F-10 | Med | Interop list is far short of the nine programs: no FBX, SKP, RVT, PLN, .blend, USD, DAE. DWG goes through DXF so DWG-native entities (3D solids, dynamic blocks, xrefs, layouts) are not carried. SAT/SAB layouts "from recollection" with no real file; IFC "validated" only in a comment. `INTEROP_LIMITATIONS.md` is stale (says no ACIS; DXF/DWG import coverage). | `FileSat.h:1-30`; `FileExchange.h:64-93`; `INTEROP_LIMITATIONS.md:5` | Update the doc; add a fixture corpus from the Observer's licensed copies (DWG, FBX, SKP exports); add IfcOpenShell or an equivalent validator to CI for IFC. |
| F-11 | Med | Documentation numbers disagree: catalogue 1071 (`smoke.sh` first check), docs site total 1055, README/TESTING "1055", AUDIT.md "1003 Implemented / 52 Partial" vs `docs/site/commands.json` 1054/1/0; TESTING.md still says annotation, blocks, Grasshopper and rendering "open their reference help instead of running". | `TESTING.md:60-63`; `AUDIT.md` section 0; `docs/site/commands.json` header | Generate every count from one script in CI and fail on drift; delete or date-stamp hand-written audit prose. |
| F-12 | Med | Scripting fidelity is dialect-level only. Python module is `dino8` (not `rhinoscriptsyntax`/RhinoCommon/`bpy`), Windows ships without Python; Lisp is a subset (no DCL, reactors); Lua aliases the Rhino name. No test runs real scripts from the baseline programs. | `PythonEngine.h:1-12`; `Lisp.h:1-12`; `LuaEngine.cpp:1385` | Collect public-API function lists per baseline into the Register, count API-level coverage, test with representative public scripts (Observer corpora). |
| F-13 | Med | Plug-in trust model: native libraries load from an index whose `sha256` can be empty; no publisher signature on plug-ins; the project's own RSA (BigUint, from-scratch primes, no known-answer test found in `test_digital_signature.cpp`) is used only for exported files. | `Marketplace.cpp:157,413`; `DigitalSignature.h:1-24` | Require hash on every downloadable entry; sign the index; use a vetted crypto library or no custom crypto; add known-answer vectors. |
| F-14 | Med | Grasshopper parity: ~98 literal node definitions (Maths 19, Curve 17, Sets 15, Vector 13, Surface 13, Params 8, Transform 7, Mesh 5, Solver 2, Display 2, Output 1) vs 773 Register rows; no `.gh`/`.ghx` import; no third-party plug-in route. Row-node tests are strong (closed forms), geometry nodes are thin. | `src/flow/FlowNodes*.cpp` | Prioritise by Register row weight; import `.gh` via public format notes if allowed. |
| F-15 | Low | Performance claims are internal ratios without thresholds, run as ctest (pass unless crash). `RHINO8_KILLER_AUDIT.md` itself says 50k-object scenes were never tested. | `tests/find_bench.cpp`; CMake `add_test(dino8_*_bench)` x8 | Add budget assertions (ms at N objects) and one comparison to a baseline timing recorded by the Observer. |
| F-16 | Low | `docs/site/` and tutorials are our own prose; Dear ImGui default font and procedural `Icons.cpp` (756 lines, ImDrawList code) are our own; OK. i18n provenance unrecorded. | `src/ui/Icons.cpp`; `data/i18n` | Add a provenance line per language file (machine/human, reviewer). |

## 4. Test-quality sample (30 items across families)

How each is wired: "smoke" = `tests/smoke.sh` or a `sections/*.sh` file that runs `Dino8 --script`; "ctest" = executable in CTest. Numeric? Y = compares a computed number to a closed form or measured value; P = integer counts/pins of own output; N = message text or no-crash.

| # | Test | Wiring | What it asserts | Numeric? |
|---|---|---|---|---|
| 1 | `boolean_script.txt` (smoke `bocheck`) | smoke | Message strings with face and crease counts ("760 face(s), 32 crease edge(s)"), exact B-rep path message, mesh union "36 faces, volume 14" | P (pins of own output) |
| 2 | `fillet_script.txt` (`flcheck`) | smoke | FilletEdge volume 991.4 (= 1000 - 10*4*(1-pi/4)), chamfer volume 955, quarter-cylinder area 31.41 | Y (closed form, prefix-match 4 digits) |
| 3 | `dwg_script.txt` + `dwg_reopen` (`dwcheck`) | smoke | Export wrote file; import reads 3 curves; `AcadSchemes` versions; DWG magic bytes for 3 releases | P / byte-level |
| 4 | `ifc_script.txt` (`ifccheck`) | smoke | File has `IFC4` schema and `IFCTRIANGULATEDFACESET`; own import reads "14 vertices, 24 faces"; refuses a point-only selection | P (self round-trip; IfcOpenShell not run) |
| 5 | `gltf_io_script.txt` + `gltf_oracle.py` | smoke (`GLT_`) | Independent parser: container lengths, accessor min/max vs decoded data, normals unit length, indices in range, round-trip object count | Y |
| 6 | `rwi_sat_script.txt` (`rwi_oracle`) | smoke | Independent Python SAT reader: 1 body, 6 faces, Euler 2, bbox, volume 1000; import/export idempotent | Y (oracle is our own) |
| 7 | `94_rwdm_rotate.sh` | smoke | 4 points after Rotate equal closed-form rotation to 1e-9 (residual 0); a 1e-6 error is visible (negative control) | Y |
| 8 | `94_frfact.sh` (`flowrows_frfact`) | smoke | Dino Flow Factorial node: 10! = 3628800, 0! = 1, 5! = 120, "solved 9 node(s)", 3 baked points | Y (exact) |
| 9 | `94_ddimbase.sh` + `dd_check.py dimchain` | smoke | DimBaseline reads 9/15/10 and exported DXF dimension chain parses with expected offsets | Y |
| 10 | `arch_script.txt` (`archeck`) | smoke | "Wall #1 added.", "Column #7 rebuilt (Height = 5.000000)", schedule counts 8 then 7; wall is one mesh after two cuts | N/P (no geometry values) |
| 11 | `constraints_script.txt` (`cncheck`) | smoke | Solver outputs: "Distance #4: length 10", "Radius #5: radius 5", "Perpendicular #6: angle 90" | Y (exact) |
| 12 | `elec_script.txt` (`elcheck`) | smoke | Bounding boxes equal parameters (0.6x0.4 resistor, 0.1 capacitor gap, switch blade tip) | Y but circular (echoes inputs) |
| 13 | `standards_script.txt` (`stcheck`) | smoke | CheckStandards: 0 violations, then exactly 3 with named causes; compliant layers not flagged | P (logic, with negative checks) |
| 14 | `sheetset_script.txt` (`sscheck`) | smoke | Sheet set create/add/open lists 2 sheets, "2/2 sheet(s) plotted" | P |
| 15 | `subd_script.txt` (`sdcheck`) | smoke | "Crease: 2 edge(s)", extrude "4 side face(s)", bridge "22 control faces" (6+4+5+inset arithmetic) | P |
| 16 | `smartblocks_script.txt` (`sbcheck`) | smoke | Detect finds 1 group of 3 instances x 3 objects; object count unchanged (13); selection 9 | P |
| 17 | `ssao_script.txt` | smoke (BMP + python) | AO darkens ambient near an inside corner, never brightens, toggling back reproduces; `gl_error=0` | Y relative (no reference image) |
| 18 | `shadow_script.txt` | smoke (BMP + python) | Two directional lights each cast a shadow in one frame | Y relative |
| 19 | `render_hdr` raytrace block | smoke | `.hdr` and `.exr` written (24x24); pixel values above 1.0 present (unclamped radiance); non-HDR render refuses `.hdr` | Y (property, not luminance) |
| 20 | `test_bvh.cpp` | ctest `dino8_bvh` | BVH hits identical to brute force over random triangles (regression for a real bug) | Y (exact) |
| 21 | `test_g2_blend.cpp`, `test_variable_blend.cpp` | ctest | Curvature magnitude equals 1/R within 1e-6 / 1e-3 | Y |
| 22 | `plugin_marketplace_script.txt` (`pmcheck`) + 16 sibling scripts | smoke | Index lists 4 plug-ins; filter "mesh" -> 1; "sample" -> 4; install, update, verify, uninstall, dependency cycle, traversal | P |
| 23 | `flow_script.txt` (`flcheck`) | smoke | Loader finds 4 plug-ins; MeshTools registers 3 nodes; `add_mesh` rejects an out-of-range face index | P |
| 24 | `clipboard_script.txt` (`cbcheck`) | smoke | Messages "image copied to the system clipboard (image/png)"; debug file has PNG signature | N/P (no OS clipboard read on Windows/macOS) |
| 25 | `python_script.txt` | smoke | `RunPythonScript` creates objects (`@expect_objects 37`); skipped where Python is OFF | P / skipped on Windows |
| 26 | `lisp_script.txt` (`LSP_`) | smoke | Each typed expression compared with an independently computed value; syntax and runtime error controls; runaway loop guard | Y |
| 27 | `ui_script.txt` | smoke | Viewport layout coordinates: width vs height within 5% (python one-liner) | Y (layout only) |
| 28 | `test_i18n_coverage`, `test_i18n_cmddesc` | ctest | Every key present in all 11 languages; translated command descriptions differ from English and are non-empty | P (cannot judge quality) |
| 29 | `test_digital_signature.cpp` | ctest | Sign/verify round trip with the hand-rolled RSA; no known-answer vector found by keyword search | P |
| 30 | `find_bench.cpp` and 7 sibling `*_bench` | ctest | Prints old-vs-new timing ratio of our own code; no threshold found | N (cannot fail on slowness) |

Reading of the sample: about one third (items 2, 5-9, 11, 17-21, 26) check a number that does not come from the same code that produced
it; another third pin counts or strings from our own output; the rest assert that a path ran. None is calibrated against a baseline program.

## 5. Licence / contamination notes

1. Rhino help text (F-01). `data/commands.json` reproduces McNeel reference text and structure (name, description, toolbars, menu paths,
   option names, full help body). 1071 names plus option lists are interface identifiers (low risk); the descriptions and help bodies are
   authored prose (high risk). Derived copies: `docs/site/commands_data.js`, `docs/site/commands.json`, `docs/site/commands.html`, translated
   subset in `data/i18n/*.json` `cmddesc.*` keys (not checked for how many).
2. Hatch library. `data/hatchpatterns.pat` states "AutoCAD .pat syntax" (syntax is fine). The pattern names and descriptions for ANSI31-ANSI38
   ("ANSI Iron, Brick, Stone masonry", "ANSI Bronze, Brass, Copper", ...), ISO02W100-ISO15W100 ("ISO dashed", "ISO long dash dot", ...),
   BRICK, HONEYCOMB, DOTS, CROSS, GRASS, GRAVEL, NET match the wording of Autodesk's `acad.pat` that I know of; numeric line families
   (45,0,0,0,.125 and similar) are simple, standard-defined geometry. Reword descriptions and regenerate numbers from ISO 128-50 and ANSI Y14.2
   definitions, record the source in the file header.
3. LibreDWG (GPLv3) is fetched, patched (`cmake/patch_libredwg.cmake`) and linked; see F-06. Other dependencies are permissive
   (OpenNURBS BSD-style, Manifold Apache-2.0, GLFW zlib, Dear ImGui MIT, Lua MIT, pybind11 BSD, Python PSF). `THIRD_PARTY_LICENSES.md` is accurate
   and candid on LibreDWG. Python is linked from the build machine, not vendored.
4. Blender (GPL) and others: no Blender source, `.blend` code, or GPL text found in `dino8-app`; no Blender-derived names beyond generic mesh terms.
   Flow tests mention "Blender geometry-node rows" only as Register row labels.
5. Icons: `src/ui/Icons.cpp` draws procedural vector icons (no bitmaps from a vendor); `resources/dino8.png/.ico` are the app's own mark (not inspected
   visually). `touch/web` holds three icon files (`icon.svg`, `icon-192.png`, `icon-512.png`) not inspected for origin. i18n strings: 258 UI keys, own short phrases; two mentions of "Rhino" in `en.json`.
6. API naming copy: Lua global `rhinoscriptsyntax` alias and Rhino-style command vocabulary are deliberate compatibility choices; names are
   interfaces, but the README's "full reference help text" is not (F-01). The "RHINO8_KILLER_AUDIT.md" and "coming-from-rhino.html" pages
   compare against Rhino by name; check trademark wording before public release.
7. Root `LICENSE` names "OpenAI" as copyright holder (template residue); the owner's stated licence ("free, open source, never sold for gain") is
   not expressed in any licence text found in `dino8-app/`.
8. Tutorials: 10 own scripts and a recording pipeline; no text from vendor training manuals was identified by a quick read of the README and
   file names (full text not compared).

## 6. What I could not check

* Whether any test is green now (no CI logs, no build). All REAL ratings mean "wired", not "observed passing".
* The 490 unmerged app commits on `kvlite/*` and other branches, including newer AutoLISP, BIM calc, glTF and fillet work.
* Pixel and timing behaviour of the renderers; accuracy of IFC/SAT output in real third-party readers.
* The full text overlap between `commands.json` and McNeel's help (I only counted vendor markers) and between `hatchpatterns.pat` and `acad.pat`
  (no copy of acad.pat available to diff; judgement from memory of its public wording).
* Whether the Windows installer ships a Python runtime (only `CMakeLists.txt` default read).
* Touch web client, packaging scripts, `dino8-kernel` tests.
