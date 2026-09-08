# Dino 8 vs. Rhino 8: Full Self-Audit ("Does Dino 8 beat Rhino 8 at literally everything?")

Date: 2026-09-08
Scope: every axis a user would judge a NURBS/mesh/SubD modeler on, not just command-name coverage.
Method: for each category, I inspected the actual Dino 8 source/build/CI (cited by file) and compared it
against Rhino 8's real, shipping behavior (McNeel's commercial release, version 8, as of this audit).
This is a self-critical audit, not a marketing sheet: **honest "loses" are called out explicitly, and
every one gets a concrete task in the backlog at the end.**

## Verdict up front

**No. Dino 8 does not beat Rhino 8 at everything today.** It matches or exceeds Rhino 8 on cost,
command-name breadth, and a handful of specific things (see wins below). It loses — sometimes badly —
on kernel robustness, performance/scale, rendering quality, ecosystem, platform reach, polish, trust,
and support. Command coverage (1031/24/0 of 1055) measures "does a command exist and produce a
plausible result on a simple test case," not "does it hold up on the hostile, messy, huge, decades-old
files real users throw at Rhino." Those are different bars, and Dino 8 has only cleared the first one.

## Scoreboard

| # | Category | Dino 8 | Rhino 8 | Verdict |
|---|---|---|---|---|
| 1 | Price / licensing | Free, no account, no telemetry (`grep` confirms, see AUDIT.md §4.4) | ~$995 perpetual (commercial), subscription options, node-locked or cloud-zoo licensing, requires online activation | **Dino 8 wins** |
| 2 | Command-name breadth | 1031/1055 catalog commands Implemented, 24 honestly Partial, 131 bonus non-catalog commands | 1055 (defines the catalog) | **Dino 8 ties/slightly wins on raw count**, but see #3 |
| 3 | Command *robustness/depth* | Each command verified against 1-3 simple smoke-test scenarios only; no fuzz/stress testing, no adversarial geometry corpus | 30 years of bug-fix history against millions of real, messy user files (degenerate curves, huge assemblies, bad topology) | **Rhino 8 wins, decisively** |
| 4 | NURBS/B-rep kernel maturity | OpenNURBS (McNeel's own free geometry library) + Manifold for booleans; no proprietary robustness layer on top | OpenNURBS + McNeel's proprietary, decades-tuned solid modeling kernel (boolean/fillet/intersection solvers hardened against degenerate cases OpenNURBS alone doesn't handle) | **Rhino 8 wins** |
| 5 | Undo/Redo | Full-document snapshot on every change (`src/doc/Document.h:4-8`) — simple and can never "forget" an inverse, but O(document size) time/memory per undo step | Incremental/per-operation undo; users report occasional undo bugs, but it scales to huge documents | **Rhino 8 wins on scalability**; Dino 8's reliability claim is real but the perf tradeoff is a real cost |
| 6 | Performance on large documents | No profiling infrastructure found (`grep` for perf/benchmark harnesses: none); no spatial acceleration beyond `Bvh` used for meshes; single-threaded document core except the path tracer and the SpaceMouse input thread (`src/render/PathTracer.cpp:564,662`, `src/input/SpaceMouse.cpp:110`) | Multi-threaded display pipeline, GPU-accelerated Cycles-based real-time viewport (Rhino Render / Raytraced mode), decades of large-assembly performance work | **Rhino 8 wins** |
| 7 | Rendering quality (offline) | CPU-only path tracer, 48-material library (`src/render/PathTracer.*`), no GPU acceleration, no denoiser mentioned, no spectral/volumetric/subsurface pipeline confirmed | Cycles (GPU + CPU, denoising, volumetrics, SSS, full PBR material graph), decades of render-farm/plugin ecosystem (V-Ray, Enscape, KeyShot, Twinmotion, Maxwell...) | **Rhino 8 wins, decisively** |
| 8 | Rendering quality (real-time viewport) | OpenGL 3.3 fixed pipeline-ish viewport, basic display modes | Modern raytraced/rendered display modes with GPU denoising, ambient occlusion, screen-space reflections | **Rhino 8 wins** |
| 9 | File format breadth | `.3dm`, `.obj`, `.stl`, `.step`/`.stp`, `.iges`/`.igs`, `.dxf`, `.pdf`, `.ply`, `.svg` (confirmed by grep of `cmd_file.cpp`/`cmd_exchange*.cpp`) | All of the above plus native DWG read/write, SAT/ACIS, Parasolid (x_t/x_b), JT, CATIA, SolidWorks, Revit (via plugins), FBX, glTF, USD, VDA, and more | **Rhino 8 wins** |
| 10 | Scripting/automation | Embedded Lua 5.4 (`src/scripting` — session-verified), a from-scratch plug-in C ABI (`include/dino8_plugin.h`, `plugins/sample/`) | RhinoScript (VBScript-era, legacy), full Python 3 (built-in), RhinoCommon .NET SDK (C#/VB.NET/F#), C++ SDK, IronPython — decades of tutorials, Stack Overflow answers, and existing scripts | **Rhino 8 wins** — Lua-only is a real gap; there is no Python or .NET story at all |
| 11 | Visual scripting | Dino Flow: custom Grasshopper-class node editor + plug-in node registration (`src/flow/*`) | Grasshopper: 15+ years of maturity, hundreds of third-party plugin packages (Kangaroo, LunchBox, Ladybug, Galapagos solvers, etc.), huge user-generated definition ecosystem on food4rhino | **Rhino 8 wins** — Dino Flow proves the concept but has ~4-node test graphs, no solver components, no genetic/evolutionary optimizer, no data-tree concept (lists/graft/flatten), no plugin marketplace |
| 12 | Plugin ecosystem | One first-party "HelloDino" sample plugin; a C ABI exists but zero third-party plugins | Hundreds of commercial and free plugins (VisualARQ, RhinoNest, Flamingo, Brazil, PanelingTools, Grasshopper plugins numbering in the thousands) | **Rhino 8 wins, decisively** — this is an ecosystem-network-effect gap that cannot be closed by engineering alone |
| 13 | Platform reach | Windows, Linux, macOS, Windows-on-ARM (desktop only) | Windows, macOS, **and Rhino for iPad** (a real, separate touch-first app), Rhino Compute (cloud/server-side geometry), ShapeDiver (cloud viewer/app builder) | **Rhino 8 wins** — no tablet/touch or cloud story at all |
| 14 | Installer trust | Unsigned installers on all platforms (no `signtool`/`codesign` in CI — confirmed by grep of `dino8-app.yml`) | Code-signed installers, notarized on macOS, published through an established, trusted vendor | **Rhino 8 wins** — unsigned installers trigger SmartScreen/Gatekeeper warnings that will scare away exactly the users Dino 8 needs to win over |
| 15 | Stability / QA maturity | One `tests/smoke.sh` script (scripted scenarios, ~949 checks) + a single unit-test binary (`dino8_test_brep_mesher`) | Decades of bug bounty/QA cycles, a public bug tracker (McNeel Discourse), service releases (8.x) driven by a huge active user base | **Rhino 8 wins, decisively** — Dino 8's test suite is "does it not crash on ~30 scripted scenarios," which is nowhere near production QA |
| 16 | Documentation / learning resources | `AUDIT.md`, this file, and inline command help text; no user manual, no video tutorials, no searchable online docs site | McNeel's extensive official docs, wikis, YouTube channel, in-person and online training, books (e.g. "The Rhino Bible"), a massive Discourse community | **Rhino 8 wins, decisively** |
| 17 | Localization | English only (no `gettext`/`.po`/i18n infrastructure found in source) | Ships in ~10+ languages | **Rhino 8 wins** |
| 18 | Accessibility | No screen-reader support, no high-contrast/accessibility mode found; ImGui-based UI has no accessibility tree | Also weak on accessibility historically, but has some keyboard-driven workflows refined over decades | **Rough tie, mild Rhino 8 edge** |
| 19 | UI/UX polish | ImGui docking UI, icon toolbars, autocomplete (prefix-only), light/dark themes (session-verified) | Decades-refined UX: fuzzy command search, extensively tested icon/toolbar system, high-DPI polish across many OS versions, muscle-memory compatibility for a huge existing user base | **Rhino 8 wins on polish**; Dino 8's autocomplete being prefix-only (not fuzzy) is a specifically documented regression vs. Rhino 8's Eto-based command box |
| 20 | Document collaboration / multi-user | None found (single-document, single-user desktop app; no tabs/MDI per `AUDIT.md` §3 item #18) | Also weak here — no real-time multi-user co-editing — but has Worksessions (shared referencing) more maturely, plus ShapeDiver/cloud viewer sharing workflows | **Rhino 8 wins narrowly** |
| 21 | Digitizer / hardware input | SpaceMouse support is real (`src/input/SpaceMouse.*`); physical 3D digitizer arm support is a documented stub (Partial, hardware unavailable in this environment) | Full digitizer-arm support (a legacy but real, certified hardware integration) | **Rhino 8 wins** (niche, but a real gap) |
| 22 | GD&T / technical drawing / tables | Real implementation this session: GD&T annotations, tables, BoM, 33-pattern hatch library — but non-associative/baked geometry, not live-linked to the model (`AUDIT.md` §3 items #21/#25) | Native, associative dimensioning/annotation tied to model updates, mature layout/plotting pipeline meeting real drafting standards in production CAD shops | **Rhino 8 wins** on associativity even though Dino 8 now has the same nominal command set |
| 23 | Fillets / booleans reliability | Session work replaced approximate fillets with exact trimmed B-rep fillets matching analytic volumes on the test corpus (`AUDIT.md` §3 item #14) — genuinely improved, but only verified against the smoke-test corpus, not against Rhino's famously hard "torture test" fillet cases | Also famous for fillet/boolean failures on hard cases, but has 30 years of targeted fixes for specific failure patterns | **Too close to call without a much larger adversarial test corpus — treat as unresolved, not a win** |
| 24 | Construction history / associativity | None (documented gap; annotation objects and Dino Flow bakes are static, not live) | Full history-enabled workflow for many commands (optional, well-integrated) | **Rhino 8 wins** |
| 25 | Total cost of ownership for a studio | $0, indefinitely, per seat | $995+ per seat (perpetual) or subscription; but includes support, training, and an ecosystem that often pays for itself in productivity | **Dino 8 wins on sticker price; unclear on total value** — the report should not oversell this as an unambiguous win, since ecosystem/plugin/training costs can eat the savings for a professional shop |

## Where Dino 8 genuinely wins today

1. **Price and licensing model.** Zero cost, zero accounts, zero telemetry, verified by direct source grep (no "subscription", "license key", "payment", or "telemetry" code paths — only free-software statements). This is real and durable.
2. **Command-name coverage.** 1031/1055 + 131 bonus commands is a larger nominal command surface than most Rhino competitors ever attempt, and it was earned honestly (verified via the real `Application::RegisterCommands()` order, not an inflated naive scan).
3. **A handful of specific technical claims that are honestly true and cite-able:** the fixed `ShrinkWrap` SDF/marching-cubes implementation, the real B-rep edge fillets matching analytic volumes on tested cases, a real (if CPU-only) path tracer, real SpaceMouse support, real sketch constraints. These are not stubs — they do real work, which is more than most "modeler in a weekend" clones manage.
4. **Undo reliability claim.** The full-snapshot model genuinely cannot exhibit the "undo silently stops working" class of bug that Rhino users do occasionally report — this is a legitimate, if narrow, correctness win, at a real and documented performance cost.

## Where Dino 8 loses, and why it matters

The most important losses, in order of how much they'd matter to a real user deciding between the two products:

1. **Kernel/algorithm robustness on hard, real-world geometry.** This is the single biggest gap. Command coverage says "FilletEdge exists and works on a 10x10x10 box." Rhino's 30-year edge in this exact area is surviving the fillet/boolean/intersection failures that come from real, messy, imported geometry (bad tangencies, near-degenerate surfaces, huge tolerances). Dino 8 has no adversarial test corpus and no evidence it survives these cases.
2. **Performance at scale.** No profiling, no large-assembly stress test, a full-document-snapshot undo, and a mostly single-threaded core. A 50,000-object Rhino file that pans and orbits smoothly in Rhino 8 has never been tested in Dino 8 and likely will not perform comparably.
3. **Rendering quality.** CPU-only path tracing vs. Rhino's GPU-accelerated Cycles integration plus its entire third-party render-plugin ecosystem (V-Ray, KeyShot, Enscape, etc.) is not a close contest.
4. **Ecosystem network effects.** Grasshopper's plugin ecosystem, Python/RhinoCommon scripting maturity, and thousands of existing plugins/definitions cannot be matched by engineering effort alone — this requires either years of organic third-party adoption or a deliberate compatibility strategy (see backlog item 5 below).
5. **Trust and distribution.** Unsigned installers are a first-impression killer: Windows SmartScreen and macOS Gatekeeper will actively warn users away before they ever see the free command coverage.
6. **QA maturity.** ~949 scripted smoke checks is meaningful regression protection for *this* team's own changes, but it is not remotely comparable to Rhino's real-world QA surface (millions of user-files, decades of bug reports).
7. **No touch/tablet or cloud story.** Rhino for iPad and Rhino Compute/ShapeDiver represent entire product lines Dino 8 has no answer to.
8. **Associativity.** GD&T/tables/annotations/Dino Flow bakes are all static/baked, not live-linked — a meaningful workflow regression for any iterative design process.

## Backlog: what it would take to actually "destroy Rhino 8"

Grouped by priority (P0 = existential gaps that block "better than Rhino at everything"; P1 = large but tractable; P2 = polish/ecosystem plays that take time regardless of engineering effort).

### P0 — kernel and performance (the credibility gaps)
- [ ] Build a large adversarial geometry test corpus (hundreds of real-world "hard" .3dm/STEP files: bad tangencies, huge tolerances, degenerate surfaces, deeply nested blocks) and run every boolean/fillet/intersection command against it; fix failures until the pass rate is at least comparable to Rhino's on the same corpus.
- [ ] Add real performance benchmarking: a large-assembly stress test (50k+ objects), profile the render/pick/select hot paths, and add spatial acceleration (BVH/octree) wherever it's missing outside meshing.
- [ ] Replace or augment full-document-snapshot undo with incremental/diff-based undo for large documents, without regressing the reliability property (e.g., diff-based snapshots with periodic full checkpoints).
- [ ] Multi-thread the document/command core beyond the path tracer (parallel meshing, parallel boolean batch operations, background file I/O).
- [ ] Add GPU-accelerated real-time rendering (raytraced/AO/denoised display mode), not just a CPU offline path tracer.

### P1 — ecosystem, trust, and scripting
- [ ] Ship a real Python 3 scripting API (and ideally a C#/.NET-compatible SDK) alongside Lua — Lua-only scripting is a serious adoption blocker for the exact power-users who'd otherwise switch.
- [ ] Sign and notarize installers on all platforms (Authenticode on Windows, notarization on macOS) to eliminate SmartScreen/Gatekeeper friction.
- [ ] Expand Dino Flow: data-tree semantics (lists/graft/flatten), a solver/optimizer node family, and a documented plugin-node SDK with at least a few real third-party-style example plugins beyond HelloDino.
- [ ] Add native DWG read/write and Parasolid/ACIS import (the highest-value missing interop formats for mechanical/AEC studios).
- [ ] Make annotation/table/Dino-Flow-bake output associative/live-updating instead of static/baked geometry.

### P2 — polish, reach, and trust-building (time/ecosystem plays)
- [ ] Real user documentation: a searchable docs site, video tutorials, and a public community forum — the current AUDIT/README-level documentation is not user-facing.
- [ ] Localization (start with the top 5-10 languages Rhino ships).
- [ ] A touch-first companion app or at least a responsive/tablet-friendly mode, to begin answering Rhino for iPad.
- [ ] Fuzzy command-palette search (already documented as a known regression vs. Rhino's command box).
- [ ] Accessibility pass: screen-reader labels and full keyboard-only workflows in the ImGui UI.
- [ ] A plugin marketplace/discovery mechanism, even a simple community-curated list, to start building the network effect Grasshopper/plugins have.
- [ ] Expand automated QA: fuzz testing, property-based tests on geometry operations, and continuous large-file regression testing, to start closing the QA-maturity gap over time.

## Bottom line

Dino 8 is a genuinely impressive amount of engineering for a from-scratch, free clone — the command-name coverage number is real and honestly earned, and several specific subsystems (ShrinkWrap, fillets, path tracer, SpaceMouse, constraints) do real work rather than faking it. But "beats Rhino 8 at literally everything" is not true today, and claiming otherwise would be dishonest. The gaps that matter most are not command-count gaps — they're kernel robustness on hard real-world geometry, performance at scale, rendering quality, ecosystem network effects, and installer trust. The backlog above is what closing that gap for real would require, roughly in the order it would need to happen.
