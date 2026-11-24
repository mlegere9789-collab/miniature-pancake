# Dino 8 (LegeLabs) scope and roles: read this first (owner order, 2026-11-24)

**Mission.** Dino 8 must reach 100% parity with all EIGHT baseline programs AND with Enscape, which is built into Dino 8
under the product name **Outscape**. Nine baselines in total:

| # | Program | Register rows |
|---|---|---|
| 1 | Rhino 8 | 1,356 |
| 2 | Grasshopper (plus its plug-in ecosystem) | 773 |
| 3 | Blender | 2,987 |
| 4 | SketchUp | 635 |
| 5 | Autodesk Revit | 1,034 |
| 6 | Archicad | 924 |
| 7 | Autodesk Forma | 186 |
| 8 | AutoCAD | 2,765 |
| 9 | Enscape (ships in Dino 8 as **Outscape**) | not yet built: the coordinator builds the Enscape rows from public documentation |

Register total today: 10,660 rows (programs 1-8). Dino 8 licence (owner's words): free, open source, never sold for gain.

**Chain of command.** Mike (owner) -> the cloud coordinator session ("second in command": plan, Register, CI, verification,
implementer agents) -> the Observer (the local session "Rhino 8 clean-room oracle protocol").

**The Observer's job is evidence, for every program.** The Observer produces behavioural evidence from the owner's licensed
copies: scripted cases run through each program's PUBLIC scripting/API surface, recording numbers and flags only, plus
behaviour statements in our own words. The Observer does NOT write Dino 8 implementation code (the firewall in
rhino/CLEANROOM.md applies to all nine). Implementation stays with the cloud implementers, who read only the repository.

**Owner's order (2026-11-24, recorded in G0_BLANKET.md):** proceed at full speed; the owner assumes the risk; 100% parity on all
levels is non-negotiable; use clean room, clean-room decompilation (Phase D, CLEANROOM.md v2), heavy research, and Dino 8's own
R&D wherever clean room cannot help. The Observer asks no questions: it decides, records its decisions and reports.

**Rules for all nine programs:** CLEANROOM.md v2 hard lines apply (no licence/DRM/anti-tamper circumvention, no vendor code,
identifiers, strings, assets or help text in the repo or to Implementers, no interference with vendor servers, no decompiler
output or notes off the Observer's machine).
* Blender: its source is public but GPL. Do not copy or port Blender code into Dino 8 (GPL permits commercial sale, which
  conflicts with the owner's licence intent). Behaviour evidence and algorithm cards only.
* Enscape: do not copy assets, materials, shader code, UI text or icons. Outscape is our own design validated by measured
  outputs (luminance histograms, resolution, timing) and algorithm cards.

**The Observer's work, in order.**
0. FULL AUDIT (first, in parallel with 1): audit ALL Dino 8 research and development from the first commit to now and write it to
   `parity/oracle/AUDIT/` (AUDIT.md plus one file per area): (a) research: every plan/spec/report in the repo and `docs/`
   (the Deep Dive, Rev 2 plan, ADRs, Register design), checking claims against sources, finding unsupported numbers and gaps;
   (b) development: walk `git log` of the repository and its branches (`claude/pdf-audit-i2bvwm`, `w0-*`), PARITY_MAP*.md,
   the Register (10,660 rows), tests and CI evidence: for each area state what is real (named passing test), what is claimed
   without evidence, what is wrong or fragile, and what is missing against the nine baselines; rank the findings by effect on
   parity. Audit files are plain analysis of OUR work: no Phase D material, no vendor identifiers.
1. Finish the Rhino smoke and full runs (rhino/run_here.py).
2. Per program, `parity/oracle/<program>/PLAN.md` first (what is measurable, case families, tolerances, what needs Phase D),
   then execute Phase B corpora, then Phase D algorithm cards for every divergence class or unknown. Order by row count:
   Blender, AutoCAD, Revit, Archicad, Grasshopper, SketchUp, Forma, Enscape; Rhino runs first.
3. Heavy research: for behaviours neither phase can settle, research the literature and public standards and write research
   notes with citations; Dino 8 R&D fills the rest, checked against golden vectors.
4. Report to the coordinator through the repository (push branch `observer-<date>`), never through chat transcripts.
